#pragma once

#include <vector>

namespace PodcastCleaner
{

/** Feature-based voice activity detector with a minimum-statistics noise floor.

    Speech is distinguished from non-speech (noise, silence, tonal/percussive
    junk) using the short-time signal-to-noise ratio and spectral flatness. The
    per-bin noise magnitude is tracked with a minimum-statistics estimator: the
    floor only follows the signal while speech is unlikely and is frozen during
    speech, so loud speech can never inflate the estimate. That inflation was the
    root cause of the denoiser previously biting into quiet speech and word
    endings.

    The class is stateful and designed to be fed frames in order. */
class VoiceActivityDetector
{
public:
    VoiceActivityDetector() = default;

    void prepare (int numBins);

    /** 0 (only loud speech counts) .. 1 (very sensitive). Default 0.5. */
    void setSensitivity (float newSensitivity) noexcept;

    void reset();

    /** Analyses one magnitude spectrum (numBins values), updates the noise
        estimate and returns the speech probability in [0, 1]. */
    float analyseFrame (const float* magnitude, int numBins);

    /** Current per-bin noise magnitude estimate. */
    const std::vector<float>& getNoiseMagnitude() const noexcept { return noiseMagnitude; }

private:
    std::vector<float> noiseMagnitude;
    std::vector<float> smoothedPower;
    std::vector<float> blockMinimum;
    std::vector<float> previousBlockMinimum;
    float sensitivity = 0.5f;
    bool initialised = false;
    int framesInBlock = 0;
};

} // namespace PodcastCleaner
