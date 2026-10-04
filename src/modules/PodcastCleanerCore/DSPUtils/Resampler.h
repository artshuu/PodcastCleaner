#pragma once

#include <vector>

namespace PodcastCleaner
{

/** Resamples a mono signal with a windowed-sinc interpolator.

    Used to bring arbitrary sample rates down to the 16 kHz that Whisper
    expects. The kernel length (taps) trades quality for speed; the default is
    more than enough for speech recognition. */
std::vector<float> resampleLinearPhase (const std::vector<float>& input,
                                        double inputRate,
                                        double outputRate,
                                        int taps = 32);

} // namespace PodcastCleaner
