#include "HighPassFilter.h"

#include <algorithm>
#include <cmath>

namespace PodcastCleaner
{

namespace
{
constexpr double twoPi = 6.283185307179586476925286766559;
}

HighPassFilter::HighPassFilter (double cutoffFrequencyHz) noexcept
    : cutoffFrequency (cutoffFrequencyHz)
{
    updateCoefficients();
}

void HighPassFilter::prepare (double newSampleRate) noexcept
{
    sampleRate = std::max (1.0, newSampleRate);
    updateCoefficients();
    reset();
}

void HighPassFilter::reset() noexcept
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void HighPassFilter::setCutoffFrequency (double cutoffFrequencyHz) noexcept
{
    cutoffFrequency = cutoffFrequencyHz;
    updateCoefficients();
}

void HighPassFilter::setQ (double newQ) noexcept
{
    q = newQ;
    updateCoefficients();
}

void HighPassFilter::updateCoefficients() noexcept
{
    const double nyquist = sampleRate * 0.5;
    const double frequency =
        std::min (std::max (cutoffFrequency, 1.0), nyquist * 0.999);
    const double effectiveQ = std::max (q, 1.0e-4);

    const double omega = twoPi * frequency / sampleRate;
    const double sinOmega = std::sin (omega);
    const double cosOmega = std::cos (omega);
    const double alpha = sinOmega / (2.0 * effectiveQ);

    const double a0 = 1.0 + alpha;
    const double a0Inv = 1.0 / a0;

    // RBJ audio-EQ-cookbook second-order high-pass coefficients.
    b0 = static_cast<float> (((1.0 + cosOmega) * 0.5) * a0Inv);
    b1 = static_cast<float> (-(1.0 + cosOmega) * a0Inv);
    b2 = b0;
    a1 = static_cast<float> ((-2.0 * cosOmega) * a0Inv);
    a2 = static_cast<float> ((1.0 - alpha) * a0Inv);
}

float HighPassFilter::processSample (float input) noexcept
{
    // Direct Form II transposed.
    const float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void HighPassFilter::process (float* samples, int numSamples) noexcept
{
    for (int index = 0; index < numSamples; ++index)
        samples[index] = processSample (samples[index]);
}

} // namespace PodcastCleaner
