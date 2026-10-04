#include "DSPStages/CensorStage.h"

#include "DSPUtils/Decibels.h"

#include <algorithm>
#include <cmath>

namespace PodcastCleaner
{

namespace
{
constexpr double twoPi = 6.283185307179586476925286766559;

std::int64_t clampIndex (std::int64_t value, std::int64_t size) noexcept
{
    return std::min (std::max<std::int64_t> (0, value), size);
}
}

void CensorStage::prepare (double newSampleRate) noexcept
{
    sampleRate = std::max (1.0, newSampleRate);
}

void CensorStage::apply (std::vector<float>& channel, const std::vector<SampleRange>& ranges) const
{
    const std::int64_t size = static_cast<std::int64_t> (channel.size());
    const int fadeLength = static_cast<int> (std::lround (fadeSeconds * sampleRate));
    const double phaseStep = twoPi * beepFrequency / sampleRate;

    for (const auto& range : ranges)
    {
        const std::int64_t start = clampIndex (range.start, size);
        const std::int64_t end = clampIndex (range.end, size);
        const std::int64_t length = end - start;
        if (length <= 0)
            continue;

        const int fade = static_cast<int> (std::min<std::int64_t> (fadeLength, length / 2));

        double sumOfSquares = 0.0;
        for (std::int64_t index = start; index < end; ++index)
        {
            const double value = channel[static_cast<size_t> (index)];
            sumOfSquares += value * value;
        }
        const double rms = std::sqrt (sumOfSquares / static_cast<double> (length));

        float beepAmplitude = 0.0f;
        if (mode == CensorMode::Beep)
        {
            const double target = rms * static_cast<double> (decibelsToGain (beepGainDb)) * 2.0;
            beepAmplitude = static_cast<float> (std::min (0.95, std::max (0.05, target)));
        }

        double phase = 0.0;
        for (std::int64_t index = start; index < end; ++index)
        {
            const std::int64_t fromStart = index - start;
            const std::int64_t toEnd = end - index;

            double envelope = 1.0;
            if (fade > 0 && fromStart < fade)
                envelope = static_cast<double> (fromStart) / static_cast<double> (fade);
            else if (fade > 0 && toEnd <= fade)
                envelope = static_cast<double> (toEnd) / static_cast<double> (fade);
            envelope = std::min (1.0, std::max (0.0, envelope));

            const float dry = channel[static_cast<size_t> (index)];
            const float wet = mode == CensorMode::Beep
                ? beepAmplitude * static_cast<float> (std::sin (phase))
                : 0.0f;

            channel[static_cast<size_t> (index)] =
                dry * static_cast<float> (1.0 - envelope) + wet * static_cast<float> (envelope);

            phase += phaseStep;
            if (phase > twoPi)
                phase -= twoPi;
        }
    }
}

} // namespace PodcastCleaner
