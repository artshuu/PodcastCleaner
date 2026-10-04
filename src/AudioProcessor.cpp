#include "AudioProcessor.h"
#include "AudioProcessorEditor.h"

#include <cmath>

namespace
{
constexpr float compressorThresholdDb = -18.0f;
constexpr float gateAttackMs = 3.0f;
constexpr float gateReleaseMs = 150.0f;
constexpr float compressorAttackMs = 10.0f;
constexpr float compressorReleaseMs = 100.0f;
constexpr float limiterCeilingDb = -1.0f;

float smoothingCoefficient(float timeMs, double sampleRate)
{
    return std::exp(-1.0f / (timeMs * 0.001f * static_cast<float>(sampleRate)));
}
}

PodcastCleanerAudioProcessor::PodcastCleanerAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
PodcastCleanerAudioProcessor::createParameterLayout()
{
    using Parameter = juce::AudioProcessorValueTreeState;
    Parameter::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"hpf", 1}, "High-pass filter", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"gate", 1}, "Noise gate", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"compressor", 1}, "Compressor", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"eq", 1}, "Podcast EQ", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"limiter", 1}, "Peak limiter", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gateThreshold", 1}, "Gate threshold",
        juce::NormalisableRange<float>{-80.0f, -20.0f, 0.5f}, -45.0f,
        juce::AudioParameterFloatAttributes().withLabel(" dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"compressionRatio", 1}, "Compression ratio",
        juce::NormalisableRange<float>{1.0f, 8.0f, 0.1f}, 3.0f,
        juce::AudioParameterFloatAttributes().withLabel(":1")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outputGain", 1}, "Output gain",
        juce::NormalisableRange<float>{-12.0f, 12.0f, 0.1f}, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel(" dB")));

    return layout;
}

void PodcastCleanerAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = juce::jmax(1.0, sampleRate);

    for (int channel = 0; channel < 2; ++channel)
        highPass[channel].prepare(currentSampleRate);

    updateFilters();
    lowMidEq[0].reset();
    lowMidEq[1].reset();
    presenceEq[0].reset();
    presenceEq[1].reset();
    gateEnvelope = 0.0f;
    compressorEnvelope = 0.0f;
    gateGain = 1.0f;
}

void PodcastCleanerAudioProcessor::releaseResources()
{
    for (int channel = 0; channel < 2; ++channel)
    {
        highPass[channel].reset();
        lowMidEq[channel].reset();
        presenceEq[channel].reset();
    }
}

bool PodcastCleanerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    const bool supportedChannels = input == juce::AudioChannelSet::mono()
        || input == juce::AudioChannelSet::stereo();

    return supportedChannels && input == output;
}

void PodcastCleanerAudioProcessor::updateFilters()
{
    const auto sampleRate = static_cast<float>(currentSampleRate);
    const auto lowMidCoefficients =
        juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate, 300.0f, 0.8f, juce::Decibels::decibelsToGain(-3.0f));
    const auto presenceCoefficients =
        juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate, 3000.0f, 0.8f, juce::Decibels::decibelsToGain(2.0f));

    for (int channel = 0; channel < 2; ++channel)
    {
        highPass[channel].setCutoffFrequency(PodcastCleaner::HighPassFilter::defaultCutoffFrequency);
        lowMidEq[channel].coefficients = lowMidCoefficients;
        presenceEq[channel].coefficients = presenceCoefficients;
    }
}

void PodcastCleanerAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int channelCount = juce::jmin(buffer.getNumChannels(), 2);
    const int sampleCount = buffer.getNumSamples();

    if (channelCount == 0 || sampleCount == 0)
        return;

    const bool useHighPass = parameters.getRawParameterValue("hpf")->load() > 0.5f;
    const bool useGate = parameters.getRawParameterValue("gate")->load() > 0.5f;
    const bool useCompressor = parameters.getRawParameterValue("compressor")->load() > 0.5f;
    const bool useEq = parameters.getRawParameterValue("eq")->load() > 0.5f;
    const bool useLimiter = parameters.getRawParameterValue("limiter")->load() > 0.5f;
    const float gateThreshold = juce::Decibels::decibelsToGain(
        parameters.getRawParameterValue("gateThreshold")->load());
    const float ratio = parameters.getRawParameterValue("compressionRatio")->load();
    const float outputGain = juce::Decibels::decibelsToGain(
        parameters.getRawParameterValue("outputGain")->load());
    const float gateAttack = smoothingCoefficient(gateAttackMs, currentSampleRate);
    const float gateRelease = smoothingCoefficient(gateReleaseMs, currentSampleRate);
    const float compressorAttack = smoothingCoefficient(compressorAttackMs, currentSampleRate);
    const float compressorRelease = smoothingCoefficient(compressorReleaseMs, currentSampleRate);
    const float limiterCeiling = juce::Decibels::decibelsToGain(limiterCeilingDb);

    auto* left = buffer.getWritePointer(0);
    auto* right = channelCount > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        float leftSample = left[sampleIndex];
        float rightSample = right != nullptr ? right[sampleIndex] : leftSample;

        if (useHighPass)
        {
            leftSample = highPass[0].processSample(leftSample);
            if (right != nullptr)
                rightSample = highPass[1].processSample(rightSample);
        }

        const float detectorLevel = juce::jmax(std::abs(leftSample), std::abs(rightSample));
        const float gateCoefficient = detectorLevel > gateEnvelope ? gateAttack : gateRelease;
        gateEnvelope = gateCoefficient * gateEnvelope
            + (1.0f - gateCoefficient) * detectorLevel;

        const float gateTarget = !useGate || gateEnvelope >= gateThreshold ? 1.0f : 0.0f;
        const float gateCoefficientForTarget = gateTarget > gateGain ? gateAttack : gateRelease;
        gateGain = gateCoefficientForTarget * gateGain
            + (1.0f - gateCoefficientForTarget) * gateTarget;
        leftSample *= gateGain;
        rightSample *= gateGain;

        const float compressorLevel = juce::jmax(std::abs(leftSample), std::abs(rightSample));
        const float compressorCoefficient =
            compressorLevel > compressorEnvelope ? compressorAttack : compressorRelease;
        compressorEnvelope = compressorCoefficient * compressorEnvelope
            + (1.0f - compressorCoefficient) * compressorLevel;

        float compressorGain = 1.0f;
        if (useCompressor && compressorEnvelope > 0.0f)
        {
            const float levelDb = juce::Decibels::gainToDecibels(compressorEnvelope);
            if (levelDb > compressorThresholdDb)
            {
                const float compressedDb = compressorThresholdDb
                    + (levelDb - compressorThresholdDb) / juce::jmax(1.0f, ratio);
                compressorGain = juce::Decibels::decibelsToGain(compressedDb - levelDb);
            }
        }

        leftSample *= compressorGain;
        rightSample *= compressorGain;

        if (useEq)
        {
            leftSample = presenceEq[0].processSample(
                lowMidEq[0].processSample(leftSample));
            if (right != nullptr)
                rightSample = presenceEq[1].processSample(
                    lowMidEq[1].processSample(rightSample));
        }

        leftSample *= outputGain;
        rightSample *= outputGain;

        if (useLimiter)
        {
            leftSample = juce::jlimit(-limiterCeiling, limiterCeiling, leftSample);
            rightSample = juce::jlimit(-limiterCeiling, limiterCeiling, rightSample);
        }

        left[sampleIndex] = leftSample;
        if (right != nullptr)
            right[sampleIndex] = rightSample;
    }
}

juce::AudioProcessorEditor* PodcastCleanerAudioProcessor::createEditor()
{
    return new PodcastCleanerAudioProcessorEditor(*this);
}

void PodcastCleanerAudioProcessor::getStateInformation(juce::MemoryBlock& destinationData)
{
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destinationData);
}

void PodcastCleanerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PodcastCleanerAudioProcessor();
}
