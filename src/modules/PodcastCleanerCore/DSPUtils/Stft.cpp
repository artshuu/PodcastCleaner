#include "Stft.h"

#include <algorithm>
#include <cmath>

namespace PodcastCleaner
{

namespace
{
constexpr double twoPi = 6.283185307179586476925286766559;
constexpr float normaliseEpsilon = 1.0e-8f;
}

void Stft::prepare (int fftOrder, int hopDivisor)
{
    const int safeOrder = std::max (1, fftOrder);
    fftSize = 1 << safeOrder;
    fft.prepare (safeOrder);

    const int safeDivisor = std::max (1, hopDivisor);
    hop = std::max (1, fftSize / safeDivisor);

    numBins = fftSize / 2 + 1;
    halfWindow = fftSize / 2;

    buildWindow();
}

void Stft::buildWindow()
{
    window.assign (static_cast<size_t> (fftSize), 0.0f);
    for (int index = 0; index < fftSize; ++index)
    {
        // Periodic (not symmetric) Hann window.
        const double phase = twoPi * static_cast<double> (index) / static_cast<double> (fftSize);
        window[static_cast<size_t> (index)] =
            static_cast<float> (0.5 * (1.0 - std::cos (phase)));
    }
}

int Stft::numFramesFor (int numSamples) const noexcept
{
    if (fftSize <= 0 || hop <= 0)
        return 0;

    return 1 + static_cast<int> (std::ceil (static_cast<double> (std::max (0, numSamples)) / hop));
}

void Stft::frameToSpectrum (const std::vector<float>& signal, int center,
                            SpectralFrame& frame,
                            std::vector<std::complex<float>>& scratch) const
{
    if (static_cast<int> (scratch.size()) != fftSize)
        scratch.assign (static_cast<size_t> (fftSize), std::complex<float> (0.0f, 0.0f));

    const int start = center - halfWindow;
    const int length = static_cast<int> (signal.size());

    // Only the samples that fall outside the signal need to be zeroed; interior
    // frames are filled completely, so the full-buffer clear is skipped.
    const int firstValid = std::max (0, -start);
    const int lastValid = std::min (fftSize, length - start);
    if (firstValid > 0)
        std::fill (scratch.begin(), scratch.begin() + firstValid, std::complex<float> (0.0f, 0.0f));
    if (lastValid < fftSize)
        std::fill (scratch.begin() + std::max (0, lastValid), scratch.end(),
                   std::complex<float> (0.0f, 0.0f));
    for (int offset = std::max (0, firstValid); offset < lastValid; ++offset)
        scratch[static_cast<size_t> (offset)] =
            signal[static_cast<size_t> (start + offset)] * window[static_cast<size_t> (offset)];

    fft.forward (scratch.data());

    // Every bin is written below, so no zero-fill is required here.
    if (static_cast<int> (frame.real.size()) != numBins)
    {
        frame.real.resize (static_cast<size_t> (numBins));
        frame.imag.resize (static_cast<size_t> (numBins));
    }
    for (int bin = 0; bin < numBins; ++bin)
    {
        frame.real[static_cast<size_t> (bin)] = scratch[static_cast<size_t> (bin)].real();
        frame.imag[static_cast<size_t> (bin)] = scratch[static_cast<size_t> (bin)].imag();
    }
}

void Stft::analyze (const std::vector<float>& signal, std::vector<SpectralFrame>& frames) const
{
    const int count = numFramesFor (static_cast<int> (signal.size()));
    frames.assign (static_cast<size_t> (count), SpectralFrame {});

    std::vector<std::complex<float>> scratch (static_cast<size_t> (fftSize));
    for (int frameIndex = 0; frameIndex < count; ++frameIndex)
        frameToSpectrum (signal, frameIndex * hop, frames[static_cast<size_t> (frameIndex)], scratch);
}

namespace
{
void reconstructAndAccumulate (const Fft& fft, const std::vector<float>& window,
                               const SpectralFrame& frame, int start, int length,
                               std::vector<std::complex<float>>& scratch,
                               std::vector<float>& output, std::vector<float>& windowSum)
{
    const int size = static_cast<int> (window.size());
    const int bins = static_cast<int> (frame.real.size());

    // Every complex bin is written (half-spectrum + conjugate mirror), so the
    // scratch buffer does not need to be cleared first.
    for (int bin = 0; bin < bins; ++bin)
        scratch[static_cast<size_t> (bin)] =
            std::complex<float> (frame.real[static_cast<size_t> (bin)],
                                 frame.imag[static_cast<size_t> (bin)]);
    for (int bin = bins; bin < size; ++bin)
        scratch[static_cast<size_t> (bin)] = std::conj (scratch[static_cast<size_t> (size - bin)]);

    fft.inverse (scratch.data());

    for (int offset = 0; offset < size; ++offset)
    {
        const int target = start + offset;
        if (target < 0 || target >= length)
            continue;
        const float w = window[static_cast<size_t> (offset)];
        output[static_cast<size_t> (target)] += scratch[static_cast<size_t> (offset)].real() * w;
        windowSum[static_cast<size_t> (target)] += w * w;
    }
}

void divideByWindowSum (std::vector<float>& output, const std::vector<float>& windowSum, int length)
{
    for (int index = 0; index < length; ++index)
    {
        const float sum = windowSum[static_cast<size_t> (index)];
        if (sum > normaliseEpsilon)
            output[static_cast<size_t> (index)] /= sum;
    }
}
} // namespace

void Stft::synthesize (const std::vector<SpectralFrame>& frames,
                       std::vector<float>& output,
                       int expectedLength) const
{
    output.assign (static_cast<size_t> (std::max (0, expectedLength)), 0.0f);
    std::vector<float> windowSum (static_cast<size_t> (std::max (0, expectedLength)), 0.0f);
    std::vector<std::complex<float>> scratch (static_cast<size_t> (fftSize));

    for (int frameIndex = 0; frameIndex < static_cast<int> (frames.size()); ++frameIndex)
        reconstructAndAccumulate (fft, window, frames[static_cast<size_t> (frameIndex)],
                                  frameIndex * hop - halfWindow, expectedLength,
                                  scratch, output, windowSum);

    divideByWindowSum (output, windowSum, expectedLength);
}

void Stft::process (const std::vector<float>& input,
                    const FrameCallback& modify,
                    std::vector<float>& output) const
{
    const int length = static_cast<int> (input.size());
    output.assign (static_cast<size_t> (std::max (0, length)), 0.0f);
    std::vector<float> windowSum (static_cast<size_t> (std::max (0, length)), 0.0f);

    std::vector<std::complex<float>> scratch (static_cast<size_t> (fftSize));
    SpectralFrame frame;

    const int count = numFramesFor (length);
    for (int frameIndex = 0; frameIndex < count; ++frameIndex)
    {
        const int center = frameIndex * hop;
        frameToSpectrum (input, center, frame, scratch);

        if (modify)
            modify (frame);

        reconstructAndAccumulate (fft, window, frame, center - halfWindow, length,
                                  scratch, output, windowSum);
    }

    divideByWindowSum (output, windowSum, length);
}

} // namespace PodcastCleaner
