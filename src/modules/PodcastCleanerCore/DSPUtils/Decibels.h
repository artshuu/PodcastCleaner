#pragma once

#include <algorithm>
#include <cmath>

namespace PodcastCleaner
{

/** Converts a linear gain to decibels (clamped at -240 dB for safety). */
inline float gainToDecibels (float gain) noexcept
{
    return 20.0f * std::log10 (std::max (gain, 1.0e-12f));
}

/** Converts decibels to a linear gain. */
inline float decibelsToGain (float decibels) noexcept
{
    return std::pow (10.0f, decibels * 0.05f);
}

} // namespace PodcastCleaner
