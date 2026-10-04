#pragma once

#include <complex>
#include <vector>

namespace PodcastCleaner
{

/** In-place radix-2 complex FFT.

    Self-contained (no external FFT library) so the core DSP can be built and
    unit-tested independently of JUCE. The transform size must be a power of
    two; call prepare() with the base-2 logarithm of that size before use.
*/
class Fft
{
public:
    Fft() = default;

    /** Prepares the transform for a size of 1 << order. Precomputes the
        bit-reversal permutation and the forward twiddle factors. */
    void prepare (int order);

    /** Returns the transform size (a power of two). */
    int getSize() const noexcept { return size; }

    /** Returns log2 of the transform size. */
    int getOrder() const noexcept { return order; }

    /** Forward transform, in place. Input order is natural; output is natural
        (the bit-reversal is handled internally). */
    void forward (std::complex<float>* data) const noexcept;

    /** Inverse transform, in place, normalised by 1 / size. */
    void inverse (std::complex<float>* data) const noexcept;

private:
    int size = 0;
    int order = 0;
    std::vector<int> bitReversal;
    std::vector<std::complex<float>> twiddles;

    void transform (std::complex<float>* data) const noexcept;
};

} // namespace PodcastCleaner
