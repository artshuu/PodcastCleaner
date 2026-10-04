#include "Analysis/LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace PodcastCleaner
{

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    float process (float input) noexcept
    {
        const double x = input;
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return static_cast<float> (y);
    }
};

Biquad makeHighShelf (double sampleRate)
{
    const double f0 = 1681.974450955533;
    const double gainDb = 3.999843853973347;
    const double q = 0.7071752369554196;

    const double k = std::tan (pi * f0 / sampleRate);
    const double vh = std::pow (10.0, gainDb / 20.0);
    const double vb = std::pow (vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;

    Biquad b;
    b.b0 = (vh + vb * k / q + k * k) / a0;
    b.b1 = 2.0 * (k * k - vh) / a0;
    b.b2 = (vh - vb * k / q + k * k) / a0;
    b.a1 = 2.0 * (k * k - 1.0) / a0;
    b.a2 = (1.0 - k / q + k * k) / a0;
    return b;
}

Biquad makeHighPass (double sampleRate)
{
    const double f0 = 38.13547087602444;
    const double q = 0.5003270373238773;

    const double k = std::tan (pi * f0 / sampleRate);
    const double a0 = 1.0 + k / q + k * k;

    Biquad b;
    b.b0 = 1.0 / a0;
    b.b1 = -2.0 / a0;
    b.b2 = 1.0 / a0;
    b.a1 = 2.0 * (k * k - 1.0) / a0;
    b.a2 = (1.0 - k / q + k * k) / a0;
    return b;
}

constexpr double loudnessOffset = -0.691;
}

float LoudnessMeter::integratedLoudness (const std::vector<std::vector<float>>& channels,
                                         double sampleRate)
{
    if (channels.empty() || channels.front().empty() || sampleRate <= 0.0)
        return -100.0f;

    const size_t sampleCount = channels.front().size();

    // K-weighting, applied per channel.
    std::vector<std::vector<float>> weighted (channels.size(), std::vector<float> (sampleCount, 0.0f));
    for (size_t channel = 0; channel < channels.size(); ++channel)
    {
        Biquad shelf = makeHighShelf (sampleRate);
        Biquad highPass = makeHighPass (sampleRate);
        for (size_t index = 0; index < sampleCount; ++index)
        {
            const float x = channels[channel][index];
            weighted[channel][index] = highPass.process (shelf.process (x));
        }
    }

    const size_t blockLength = static_cast<size_t> (std::lround (0.4 * sampleRate));
    const size_t hopLength = static_cast<size_t> (std::lround (0.1 * sampleRate));
    if (blockLength == 0 || sampleCount < blockLength)
        return -100.0f;

    std::vector<double> blockLoudness;
    std::vector<double> blockMeanSquare;

    for (size_t start = 0; start + blockLength <= sampleCount; start += hopLength)
    {
        double sum = 0.0;
        for (size_t channel = 0; channel < weighted.size(); ++channel)
            for (size_t index = start; index < start + blockLength; ++index)
                sum += static_cast<double> (weighted[channel][index]) * weighted[channel][index];

        const double meanSquare = sum / static_cast<double> (blockLength);
        const double loudness = loudnessOffset + 10.0 * std::log10 (meanSquare + 1.0e-12);
        blockMeanSquare.push_back (meanSquare);
        blockLoudness.push_back (loudness);
    }

    // Absolute gate.
    double sum = 0.0;
    int count = 0;
    for (size_t i = 0; i < blockLoudness.size(); ++i)
        if (blockLoudness[i] >= -70.0)
        {
            sum += blockMeanSquare[i];
            ++count;
        }

    if (count == 0)
        return -100.0f;

    const double relativeThreshold = loudnessOffset + 10.0 * std::log10 (sum / count) - 10.0;

    sum = 0.0;
    count = 0;
    for (size_t i = 0; i < blockLoudness.size(); ++i)
        if (blockLoudness[i] >= -70.0 && blockLoudness[i] >= relativeThreshold)
        {
            sum += blockMeanSquare[i];
            ++count;
        }

    if (count == 0)
        return -100.0f;

    return static_cast<float> (loudnessOffset + 10.0 * std::log10 (sum / count));
}

float LoudnessMeter::truePeakDb (const std::vector<std::vector<float>>& channels)
{
    float peak = 0.0f;
    for (const auto& channel : channels)
    {
        for (size_t index = 0; index + 1 < channel.size(); ++index)
        {
            const float a = channel[index];
            const float b = channel[index + 1];
            peak = std::max (peak, std::abs (a));
            // 4x linear interpolation to catch inter-sample peaks.
            for (int step = 1; step < 4; ++step)
            {
                const float t = static_cast<float> (step) / 4.0f;
                peak = std::max (peak, std::abs (a + (b - a) * t));
            }
        }
        if (! channel.empty())
            peak = std::max (peak, std::abs (channel.back()));
    }
    return peak > 0.0f ? 20.0f * std::log10 (peak) : -100.0f;
}

} // namespace PodcastCleaner
