#pragma once

#include <JuceHeader.h>

#include "modules/PodcastCleanerCore/DSPStages/HighPassFilter.h"

class PodcastCleanerAudioProcessor final : public juce::AudioProcessor
{
public:
    PodcastCleanerAudioProcessor();
    ~PodcastCleanerAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destinationData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateFilters();

    PodcastCleaner::HighPassFilter highPass[2];
    juce::dsp::IIR::Filter<float> lowMidEq[2];
    juce::dsp::IIR::Filter<float> presenceEq[2];

    double currentSampleRate = 44100.0;
    float gateEnvelope = 0.0f;
    float compressorEnvelope = 0.0f;
    float gateGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PodcastCleanerAudioProcessor)
};
