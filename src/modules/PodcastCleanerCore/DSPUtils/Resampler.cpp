#include "DSPUtils/Resampler.h"

#include <algorithm>
#include <cmath>

namespace PodcastCleaner
{

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;

double hann (double normalized) // normalized in [-1, 1]
{
    return 0.5 * (1.0 + std::cos (pi * normalized));
}
}

std::vector<float> resampleLinearPhase (const std::vector<float>& input,
                                        double inputRate,
                                        double outputRate,
                                        int taps)
{
    if (input.empty() || inputRate <= 0.0 || outputRate <= 0.0)
        return input;

    if (std::abs (inputRate - outputRate) < 1.0e-6)
        return input;

    const double ratio = outputRate / inputRate;
    const auto outputLength = static_cast<size_t> (
        std::max<std::int64_t> (1, static_cast<std::int64_t> (std::llround (input.size() * ratio))));

    const int halfTaps = std::max (2, taps / 2);
    const double cutoff = std::min (1.0, ratio); // anti-alias cutoff (relative to input Nyquist)

    std::vector<float> output (outputLength, 0.0f);
    const std::int64_t inputSize = static_cast<std::int64_t> (input.size());

    for (size_t outIndex = 0; outIndex < outputLength; ++outIndex)
    {
        const double position = static_cast<double> (outIndex) / ratio;
        const std::int64_t center = static_cast<std::int64_t> (std::floor (position));
        const double frac = position - static_cast<double> (center);

        double sum = 0.0;
        double weightSum = 0.0;
        for (int k = -halfTaps + 1; k <= halfTaps; ++k)
        {
            const std::int64_t index = center + k;
            if (index < 0 || index >= inputSize)
                continue;

            const double x = (static_cast<double> (k) - frac) * cutoff;
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double window = hann (static_cast<double> (k - frac) / static_cast<double> (halfTaps));
            const double weight = sinc * window;

            sum += static_cast<double> (input[static_cast<size_t> (index)]) * weight;
            weightSum += weight;
        }

        output[outIndex] = weightSum > 1.0e-9
            ? static_cast<float> (sum / weightSum)
            : 0.0f;
    }

    return output;
}

} // namespace PodcastCleaner
