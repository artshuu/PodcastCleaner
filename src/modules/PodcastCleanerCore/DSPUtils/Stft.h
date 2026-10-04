#pragma once

#include "Fft.h"

#include <functional>
#include <vector>

namespace PodcastCleaner
{

/** One half-spectrum STFT frame (bins 0 .. fftSize/2, inclusive). */
struct SpectralFrame
{
    std::vector<float> real;
    std::vector<float> imag;
};

/** Short-time Fourier transform with exact (weighted overlap-add) reconstruction.

    Uses centred frames, a periodic Hann window and a hop of fftSize /
    hopDivisor (default 4, i.e. 75% overlap). Analysis and synthesis use the
    same window, so the accumulation is divided by the summed squared window,
    which reconstructs the input exactly when the spectra are not modified.

    Two entry points are provided:
      - analyze()/synthesize() store every frame (convenient for short buffers
        and unit tests),
      - process() streams frames through a callback and keeps only the
        overlap-add accumulators (constant memory per sample, used in the
        offline pipeline for arbitrarily long files).
*/
class Stft
{
public:
    using FrameCallback = std::function<void (SpectralFrame&)>;

    Stft() = default;

    /** Prepares the transform. fftOrder sets the window length (1 << fftOrder);
        hopDivisor sets the hop as window / hopDivisor. */
    void prepare (int fftOrder, int hopDivisor = 4);

    int getFftSize() const noexcept { return fftSize; }
    int getHopSize() const noexcept { return hop; }

    /** Number of frequency bins stored per frame (fftSize / 2 + 1). */
    int getNumBins() const noexcept { return numBins; }

    /** Number of frames needed to cover a signal of the given length. */
    int numFramesFor (int numSamples) const noexcept;

    /** Returns the periodic Hann analysis window. */
    const std::vector<float>& getWindow() const noexcept { return window; }

    /** Full-buffer analysis. Fills frames with numFramesFor(signal.size())
        half-spectra. */
    void analyze (const std::vector<float>& signal, std::vector<SpectralFrame>& frames) const;

    /** Full-buffer synthesis. Produces exactly expectedLength samples. */
    void synthesize (const std::vector<SpectralFrame>& frames,
                     std::vector<float>& output,
                     int expectedLength) const;

    /** Streaming analysis/modify/synthesis. The callback receives each frame in
        order and may rewrite its real/imag bins in place; the result (same
        length as the input) is written to output. */
    void process (const std::vector<float>& input,
                  const FrameCallback& modify,
                  std::vector<float>& output) const;

private:
    void buildWindow();
    void frameToSpectrum (const std::vector<float>& signal, int center,
                          SpectralFrame& frame, std::vector<std::complex<float>>& scratch) const;

    Fft fft;
    int fftSize = 0;
    int hop = 0;
    int numBins = 0;
    int halfWindow = 0;
    std::vector<float> window;
};

} // namespace PodcastCleaner
