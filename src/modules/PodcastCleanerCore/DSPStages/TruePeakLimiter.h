#pragma once

#include <vector>

namespace PodcastCleaner
{

/** Look-ahead brick-wall limiter for mastering.

    A sliding-window maximum of the absolute signal over the look-ahead span
    drives a smooth gain envelope, so the output never exceeds the ceiling while
    transients are caught before they clip. The number of samples is unchanged.
    Intended for offline (non real-time) use. */
class TruePeakLimiter
{
public:
    void prepare (double newSampleRate) noexcept;

    void setCeilingDb (float decibels) noexcept { ceilingDb = decibels; }
    void setLookaheadMs (double ms) noexcept { lookaheadMs = ms; }
    void setReleaseMs (double ms) noexcept { releaseMs = ms; }

    /** Applies limiting to a single channel in place. */
    void process (std::vector<float>& channel);

    /** Maximum gain reduction applied during the last process() call. */
    float getGainReductionDb() const noexcept;

private:
    double sampleRate = 44100.0;
    float ceilingDb = -1.0f;
    double lookaheadMs = 5.0;
    double releaseMs = 50.0;
    float minGain = 1.0f;
};

} // namespace PodcastCleaner
