#include "DSPStages/NonSpeechSilencer.h"

#include <algorithm>
#include <cmath>

namespace PodcastCleaner
{

void NonSpeechSilencer::prepare (double newSampleRate)
{
    prepare (newSampleRate, Settings {});
}

void NonSpeechSilencer::prepare (double newSampleRate, const Settings& newSettings)
{
    sampleRate = newSampleRate;
    settings = newSettings;
    stft.prepare (settings.fftOrder, settings.hopDivisor);
    vad.prepare (stft.getNumBins());
    vad.setSensitivity (settings.sensitivity);
    speechRatio = 1.0f;
}

void NonSpeechSilencer::buildEnvelope (const std::vector<float>& mono,
                                       std::vector<float>& envelope)
{
    const int length = static_cast<int> (mono.size());
    const float floorGain = std::min (1.0f, std::max (0.0f, settings.gainFloor()));

    envelope.assign (static_cast<size_t> (std::max (0, length)), floorGain);
    if (length == 0)
    {
        speechRatio = 1.0f;
        return;
    }

    vad.reset();

    const int bins = stft.getNumBins();
    const int hop = std::max (1, stft.getHopSize());

    // Frame-rate one-pole coefficients for the attack and release fades.
    const float frameSeconds = static_cast<float> (hop) / static_cast<float> (sampleRate);
    const float attackSeconds = std::max (0.001f, settings.attackMs / 1000.0f);
    const float releaseSeconds = std::max (0.001f, settings.releaseMs / 1000.0f);
    const float attackCoeff = 1.0f - std::exp (-frameSeconds / attackSeconds);
    const float releaseCoeff = 1.0f - std::exp (-frameSeconds / releaseSeconds);
    const int holdFrames = std::max (0, static_cast<int> (
        std::lround ((settings.holdMs / 1000.0f) / frameSeconds)));

    std::vector<float> magnitude (static_cast<size_t> (bins), 0.0f);
    std::vector<float> frameEnvelope;
    frameEnvelope.reserve (static_cast<size_t> (stft.numFramesFor (length)));

    float level = floorGain;   // current smoothed gate opening (floorGain..1)
    int holdCounter = 0;
    int speechFrames = 0;

    // analyse only - the reconstructed audio is discarded, only the envelope matters.
    std::vector<float> analysisOutput;
    stft.process (mono, [&] (SpectralFrame& frame)
    {
        for (int bin = 0; bin < bins; ++bin)
            magnitude[static_cast<size_t> (bin)] =
                std::hypot (frame.real[static_cast<size_t> (bin)],
                            frame.imag[static_cast<size_t> (bin)]);

        const float probability = vad.analyseFrame (magnitude.data(), bins);

        if (probability >= settings.openThreshold)
            holdCounter = holdFrames;
        else if (holdCounter > 0)
            --holdCounter;

        const bool speech = holdCounter > 0;
        const float target = speech ? 1.0f : floorGain;
        const float coeff = target > level ? attackCoeff : releaseCoeff;
        level += coeff * (target - level);

        frameEnvelope.push_back (level);
        if (speech)
            ++speechFrames;
    }, analysisOutput);

    const int count = static_cast<int> (frameEnvelope.size());
    if (count == 0)
    {
        speechRatio = 1.0f;
        return;
    }

    // Interpolate the frame-rate envelope to per-sample. Frame f is centred at
    // sample f * hop, so sample s lies between the centres of frames f and f + 1.
    for (int sample = 0; sample < length; ++sample)
    {
        const int frame = sample / hop;
        const float fraction = static_cast<float> (sample - frame * hop)
                             / static_cast<float> (hop);
        const float a = frameEnvelope[static_cast<size_t> (std::min (frame, count - 1))];
        const float b = frameEnvelope[static_cast<size_t> (std::min (frame + 1, count - 1))];
        envelope[static_cast<size_t> (sample)] = a + (b - a) * fraction;
    }

    speechRatio = static_cast<float> (speechFrames) / static_cast<float> (count);
}

void NonSpeechSilencer::applyEnvelope (std::vector<float>& channel,
                                       const std::vector<float>& envelope)
{
    const size_t count = std::min (channel.size(), envelope.size());
    for (size_t index = 0; index < count; ++index)
        channel[index] *= envelope[index];
}

void NonSpeechSilencer::process (const std::vector<float>& input,
                                 std::vector<float>& output)
{
    buildEnvelope (input, output);
    for (size_t index = 0; index < output.size(); ++index)
        output[index] *= input[index];
}

} // namespace PodcastCleaner
