#include "DSPStages/TruePeakLimiter.h"

#include "DSPUtils/Decibels.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace PodcastCleaner
{

void TruePeakLimiter::prepare (double newSampleRate) noexcept
{
    sampleRate = std::max (1.0, newSampleRate);
    minGain = 1.0f;
}

void TruePeakLimiter::process (std::vector<float>& channel)
{
    const int length = static_cast<int> (channel.size());
    if (length == 0)
        return;

    const float ceiling = decibelsToGain (ceilingDb);
    const int lookahead = std::max (0, std::min (length - 1,
        static_cast<int> (std::lround (lookaheadMs * 1.0e-3 * sampleRate))));

    // Sliding window maximum of |x| over [i, i + lookahead].
    std::vector<float> windowMax (static_cast<size_t> (length), 0.0f);
    std::deque<int> candidates;
    int right = -1;
    for (int index = 0; index < length; ++index)
    {
        const int newRight = std::min (length - 1, index + lookahead);
        while (right < newRight)
        {
            ++right;
            const float value = std::abs (channel[static_cast<size_t> (right)]);
            while (! candidates.empty()
                   && std::abs (channel[static_cast<size_t> (candidates.back() )]) <= value)
                candidates.pop_back();
            candidates.push_back (right);
        }
        while (! candidates.empty() && candidates.front() < index)
            candidates.pop_front();
        windowMax[static_cast<size_t> (index)] =
            std::abs (channel[static_cast<size_t> (candidates.front() )]);
    }

    const double releaseSamples = std::max (1.0, releaseMs * 1.0e-3 * sampleRate);
    const float releaseCoefficient = static_cast<float> (1.0 - std::exp (-1.0 / releaseSamples));

    float current = 1.0f;
    minGain = 1.0f;
    for (int index = 0; index < length; ++index)
    {
        const float peak = windowMax[static_cast<size_t> (index)];
        const float desired = peak > ceiling ? ceiling / peak : 1.0f;

        if (desired < current)
            current = desired; // instant attack (already shaped by the look-ahead window)
        else
            current += releaseCoefficient * (desired - current);

        minGain = std::min (minGain, current);
        channel[static_cast<size_t> (index)] *= current;
    }
}

float TruePeakLimiter::getGainReductionDb() const noexcept
{
    return gainToDecibels (minGain);
}

} // namespace PodcastCleaner
