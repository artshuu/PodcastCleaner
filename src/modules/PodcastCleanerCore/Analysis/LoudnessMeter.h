#pragma once

#include <vector>

namespace PodcastCleaner
{

/** ITU-R BS.1770 / EBU R128 integrated loudness meter.

    Applies the K-weighting filter, forms 400 ms blocks with 100 ms overlap and
    performs the two-stage (absolute -70 LUFS and relative -10 LU) gating. All
    channels are given equal weight (mono / stereo). */
class LoudnessMeter
{
public:
    /** Returns the integrated loudness in LUFS. Returns -100.0 for silence. */
    static float integratedLoudness (const std::vector<std::vector<float>>& channels,
                                     double sampleRate);

    /** Returns the maximum true-peak level in dBFS, estimated with 4x linear
        interpolation between samples. */
    static float truePeakDb (const std::vector<std::vector<float>>& channels);
};

} // namespace PodcastCleaner
