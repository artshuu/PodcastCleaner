#pragma once

#include "Analysis/VoiceActivityDetector.h"
#include "DSPUtils/Stft.h"

#include <vector>

namespace PodcastCleaner
{

/** Speech-safe spectral denoiser (decision-directed Wiener filter).

    Instead of hard spectral subtraction (which produces musical noise and eats
    quiet consonants) the gain is derived from a decision-directed a priori SNR
    (Ephraim-Malah style). By construction the Wiener gain tends to unity whenever
    a bin is dominated by speech, so speech, breaths and soft word endings are
    left essentially untouched. Additional safety nets:

      - a per-bin speech factor, so noise-only bins of a speech frame are still
        suppressed while speech bins are preserved,
      - a frame-level speech probability blend from the VoiceActivityDetector,
      - 3-tap frequency smoothing and one-pole temporal smoothing of the gain,
        which keep the residual noise natural rather than pumping.

    The output always has exactly as many samples as the input - the duration
    never changes. */
class SpectralDenoiser
{
public:
    struct Settings
    {
        int fftOrder = 10;              // window length = 1 << fftOrder
        int hopDivisor = 4;             // 75% overlap
        float reductionDb = 12.0f;      // maximum attenuation of the noise floor
        float floorDb = -60.0f;         // absolute gain floor
        float sensitivity = 0.5f;       // VAD sensitivity (0..1)
        float speechProtection = 0.9f;  // 0..1, how strongly speech is preserved
        float gainSmoothing = 0.6f;     // 0..1, temporal smoothing of the gain
        float overSubtraction = 1.0f;   // noise over-estimation before the Wiener step
    };

    SpectralDenoiser() = default;

    void prepare (double sampleRate);
    void prepare (double sampleRate, const Settings& newSettings);

    /** Updates the settings; call prepare() again if fftOrder/hopDivisor change. */
    void setSettings (const Settings& newSettings) noexcept;

    void reset();

    /** Denoises input (same length) into output. Resets per-call state so the
        same instance can process several channels in sequence. */
    void process (const std::vector<float>& input, std::vector<float>& output);

    /** Fraction of frames classified as non-speech during the last process(). */
    float getNonSpeechRatio() const noexcept { return nonSpeechRatio; }

private:
    void processFrame (SpectralFrame& frame);

    Stft stft;
    VoiceActivityDetector vad;
    Settings settings;
    double sampleRate = 44100.0;
    std::vector<float> magnitude;
    std::vector<float> power;
    std::vector<float> previousPower;
    std::vector<float> previousGain;
    std::vector<float> filteredGain;
    std::vector<float> smoothedGain;
    float nonSpeechRatio = 0.0f;
    int frameCount = 0;
    int nonSpeechFrames = 0;
};

} // namespace PodcastCleaner
