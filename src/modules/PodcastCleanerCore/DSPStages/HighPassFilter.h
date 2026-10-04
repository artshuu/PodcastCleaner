#pragma once

namespace PodcastCleaner
{

/** Second-order (biquad) IIR high-pass filter.

    Provides a 12 dB/octave roll-off. The default design targets an 80 Hz
    cutoff with a maximally flat (Butterworth) response at Q = 1 / sqrt(2).

    The filter is a plain DSP primitive: it holds its own state, so a
    separate instance must be used per audio channel.
*/
class HighPassFilter
{
public:
    /** Q of a maximally flat (Butterworth) second-order low/high-pass. */
    static constexpr double butterworthQ = 0.70710678118654752440;

    /** Default cutoff frequency (Hz) used for the podcast cleanup chain. */
    static constexpr double defaultCutoffFrequency = 80.0;

    HighPassFilter() = default;

    explicit HighPassFilter (double cutoffFrequencyHz) noexcept;

    /** Sets the sample rate and recomputes the coefficients. Clears state. */
    void prepare (double newSampleRate) noexcept;

    /** Clears the internal filter state. */
    void reset() noexcept;

    /** Sets the -3 dB cutoff frequency in Hz and recomputes the coefficients. */
    void setCutoffFrequency (double cutoffFrequencyHz) noexcept;

    /** Returns the current cutoff frequency in Hz. */
    double getCutoffFrequency() const noexcept { return cutoffFrequency; }

    /** Sets the resonance/quality factor and recomputes the coefficients. */
    void setQ (double newQ) noexcept;

    /** Returns the current resonance/quality factor. */
    double getQ() const noexcept { return q; }

    /** Processes a single sample and returns the filtered value. */
    float processSample (float input) noexcept;

    /** Processes a mono block of samples in place. */
    void process (float* samples, int numSamples) noexcept;

private:
    void updateCoefficients() noexcept;

    double sampleRate = 44100.0;
    double cutoffFrequency = defaultCutoffFrequency;
    double q = butterworthQ;

    // Normalised (a0 == 1) biquad coefficients.
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    // Direct Form II transposed state.
    float z1 = 0.0f;
    float z2 = 0.0f;
};

} // namespace PodcastCleaner
