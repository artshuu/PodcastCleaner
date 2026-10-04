#include "Analysis/VoiceActivityDetector.h"

#include <algorithm>
#include <cmath>

namespace PodcastCleaner
{

namespace
{
constexpr float epsilon = 1.0e-12f;
constexpr float powerFloor = 1.0e-9f;

// Minimum-statistics tuning.
constexpr int blockLengthFrames = 48; // number of non-speech frames per block
constexpr float minimumBias = 1.5f;   // the block minimum underestimates the mean
constexpr float speechCutoff = 0.5f;  // the floor is frozen above this probability
constexpr float powerSmoothing = 0.5f;

float sigmoid (double x) noexcept
{
    return static_cast<float> (1.0 / (1.0 + std::exp (-x)));
}
} // namespace

void VoiceActivityDetector::prepare (int numBins)
{
    const size_t bins = static_cast<size_t> (std::max (1, numBins));
    noiseMagnitude.assign (bins, 0.0f);
    smoothedPower.assign (bins, 0.0f);
    blockMinimum.assign (bins, 0.0f);
    previousBlockMinimum.assign (bins, 0.0f);
    initialised = false;
    framesInBlock = 0;
}

void VoiceActivityDetector::setSensitivity (float newSensitivity) noexcept
{
    sensitivity = std::min (1.0f, std::max (0.0f, newSensitivity));
}

void VoiceActivityDetector::reset()
{
    std::fill (noiseMagnitude.begin(), noiseMagnitude.end(), 0.0f);
    std::fill (smoothedPower.begin(), smoothedPower.end(), 0.0f);
    std::fill (blockMinimum.begin(), blockMinimum.end(), 0.0f);
    std::fill (previousBlockMinimum.begin(), previousBlockMinimum.end(), 0.0f);
    initialised = false;
    framesInBlock = 0;
}

float VoiceActivityDetector::analyseFrame (const float* magnitude, int numBins)
{
    if (static_cast<int> (noiseMagnitude.size()) != numBins)
        prepare (numBins);

    if (! initialised)
    {
        for (int bin = 0; bin < numBins; ++bin)
        {
            const float p = std::max (magnitude[bin] * magnitude[bin], powerFloor);
            smoothedPower[static_cast<size_t> (bin)] = p;
            blockMinimum[static_cast<size_t> (bin)] = p;
            previousBlockMinimum[static_cast<size_t> (bin)] = p;
            noiseMagnitude[static_cast<size_t> (bin)] = std::sqrt (p * minimumBias);
        }
        initialised = true;
        return 0.0f;
    }

    double power = 0.0;
    double noisePower = 0.0;
    double logPowerSum = 0.0;
    for (int bin = 0; bin < numBins; ++bin)
    {
        const double p = static_cast<double> (magnitude[bin]) * magnitude[bin];
        power += p;
        noisePower += static_cast<double> (noiseMagnitude[static_cast<size_t> (bin)])
                    * noiseMagnitude[static_cast<size_t> (bin)];
        logPowerSum += std::log (p + epsilon);
    }

    const double arithmeticMean = power / numBins;
    const double geometricMean = std::exp (logPowerSum / numBins);
    const double flatness = geometricMean / (arithmeticMean + epsilon);
    const double snrDb = 10.0 * std::log10 ((power + epsilon) / (noisePower + epsilon));
    const double thresholdDb = 9.0 - 6.0 * static_cast<double> (sensitivity);
    const float pSnr = sigmoid ((snrDb - thresholdDb) / 4.0);
    const double tonal = std::min (1.0, std::max (0.0, (0.7 - flatness) / 0.5));
    const float probability = std::min (1.0f,
        std::max (0.0f, pSnr * static_cast<float> (0.35 + 0.65 * tonal)));

    // Track the noise floor only while speech is unlikely, so speech never
    // raises the estimate.
    if (probability < speechCutoff)
    {
        for (int bin = 0; bin < numBins; ++bin)
        {
            const float p = std::max (magnitude[bin] * magnitude[bin], powerFloor);
            float& smoothed = smoothedPower[static_cast<size_t> (bin)];
            smoothed += powerSmoothing * (p - smoothed);
            float& block = blockMinimum[static_cast<size_t> (bin)];
            block = std::min (block, smoothed);
        }

        if (++framesInBlock >= blockLengthFrames)
        {
            previousBlockMinimum = blockMinimum;
            blockMinimum = smoothedPower;
            framesInBlock = 0;
        }
    }

    for (int bin = 0; bin < numBins; ++bin)
    {
        const float minimum = std::min (blockMinimum[static_cast<size_t> (bin)],
                                        previousBlockMinimum[static_cast<size_t> (bin)]);
        noiseMagnitude[static_cast<size_t> (bin)] =
            std::sqrt (std::max (minimum, powerFloor) * minimumBias);
    }

    return probability;
}

} // namespace PodcastCleaner
