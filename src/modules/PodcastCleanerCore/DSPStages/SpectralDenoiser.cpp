#include "DSPStages/SpectralDenoiser.h"

#include "DSPUtils/Decibels.h"

#include <algorithm>
#include <cmath>

namespace PodcastCleaner
{

namespace
{
// Decision-directed a priori SNR smoothing (0.98 keeps speech harmonics alive,
// the remaining 2% lets the estimate react to onsets within a few frames).
constexpr float priorSnrHistory = 0.98f;
constexpr float priorSnrInstant = 0.02f;
constexpr float noisePowerFloor = 1.0e-12f;
}

void SpectralDenoiser::prepare (double newSampleRate)
{
    prepare (newSampleRate, Settings {});
}

void SpectralDenoiser::prepare (double newSampleRate, const Settings& newSettings)
{
    sampleRate = newSampleRate;
    settings = newSettings;
    stft.prepare (settings.fftOrder, settings.hopDivisor);
    vad.prepare (stft.getNumBins());
    vad.setSensitivity (settings.sensitivity);
    reset();
}

void SpectralDenoiser::setSettings (const Settings& newSettings) noexcept
{
    settings = newSettings;
}

void SpectralDenoiser::reset()
{
    vad.reset();
    magnitude.clear();
    power.clear();
    previousPower.clear();
    previousGain.clear();
    filteredGain.clear();
    smoothedGain.clear();
    nonSpeechRatio = 0.0f;
    frameCount = 0;
    nonSpeechFrames = 0;
}

void SpectralDenoiser::processFrame (SpectralFrame& frame)
{
    const int bins = static_cast<int> (frame.real.size());
    if (static_cast<int> (magnitude.size()) != bins)
    {
        magnitude.assign (static_cast<size_t> (bins), 0.0f);
        power.assign (static_cast<size_t> (bins), 0.0f);
        previousPower.assign (static_cast<size_t> (bins), 0.0f);
        previousGain.assign (static_cast<size_t> (bins), 1.0f);
        filteredGain.assign (static_cast<size_t> (bins), 1.0f);
        smoothedGain.assign (static_cast<size_t> (bins), 1.0f);
    }

    for (int bin = 0; bin < bins; ++bin)
    {
        const float re = frame.real[static_cast<size_t> (bin)];
        const float im = frame.imag[static_cast<size_t> (bin)];
        const float mag = std::hypot (re, im);
        magnitude[static_cast<size_t> (bin)] = mag;
        power[static_cast<size_t> (bin)] = mag * mag;
    }

    const float speechProbability = vad.analyseFrame (magnitude.data(), bins);
    const auto& noise = vad.getNoiseMagnitude();

    const float reductionGain = decibelsToGain (-std::abs (settings.reductionDb));
    const float floorGain = std::max (reductionGain, decibelsToGain (settings.floorDb));
    const float smoothing = std::min (0.99f, std::max (0.0f, settings.gainSmoothing));
    const float protect = std::min (1.0f, std::max (0.0f, settings.speechProtection))
                        * speechProbability;

    // Pass 1: decision-directed Wiener gain per bin.
    for (int bin = 0; bin < bins; ++bin)
    {
        const float noisePower = std::max (
            noise[static_cast<size_t> (bin)] * noise[static_cast<size_t> (bin)]
                * std::max (0.0f, settings.overSubtraction),
            noisePowerFloor);

        const float observedPower = power[static_cast<size_t> (bin)];
        const float posteriorSnr = observedPower / noisePower;

        // Decision-directed a priori SNR: smooths the gain over time, which both
        // preserves speech harmonics and removes musical noise.
        const float previousSnr = previousGain[static_cast<size_t> (bin)]
                                * previousGain[static_cast<size_t> (bin)]
                                * previousPower[static_cast<size_t> (bin)] / noisePower;
        const float instantaneous = std::max (posteriorSnr - 1.0f, 0.0f);
        const float priorSnr = std::max (priorSnrHistory * previousSnr
                                         + priorSnrInstant * instantaneous, 0.0f);

        float gain = priorSnr / (priorSnr + 1.0f); // Wiener, -> 1 for speech

        // Protect speech: strong bins of a speech frame are pulled to unity, so
        // intelligible content is preserved regardless of small VAD errors.
        const float binSpeech = posteriorSnr / (posteriorSnr + 1.0f);
        gain += (1.0f - gain) * protect * binSpeech;

        filteredGain[static_cast<size_t> (bin)] = gain;
    }

    // Pass 2: 3-tap frequency smoothing, temporal smoothing and application.
    for (int bin = 0; bin < bins; ++bin)
    {
        const size_t index = static_cast<size_t> (bin);
        const float left = filteredGain[static_cast<size_t> (std::max (0, bin - 1))];
        const float right = filteredGain[static_cast<size_t> (std::min (bins - 1, bin + 1))];
        const float frequencySmoothed = 0.25f * left + 0.5f * filteredGain[index] + 0.25f * right;

        const float temporal = smoothedGain[index] * smoothing
                             + frequencySmoothed * (1.0f - smoothing);
        const float applied = std::min (1.0f, std::max (temporal, floorGain));
        smoothedGain[index] = applied;

        frame.real[index] *= applied;
        frame.imag[index] *= applied;

        previousPower[index] = power[index];
        previousGain[index] = applied;
    }

    ++frameCount;
    if (speechProbability < 0.35f)
        ++nonSpeechFrames;
    nonSpeechRatio = frameCount > 0
        ? static_cast<float> (nonSpeechFrames) / static_cast<float> (frameCount) : 0.0f;
}

void SpectralDenoiser::process (const std::vector<float>& input, std::vector<float>& output)
{
    reset();
    stft.process (input, [this] (SpectralFrame& frame) { processFrame (frame); }, output);
}

} // namespace PodcastCleaner
