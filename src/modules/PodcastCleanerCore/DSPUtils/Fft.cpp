#include "Fft.h"

#include <cmath>

namespace PodcastCleaner
{

namespace
{
constexpr double twoPi = 6.283185307179586476925286766559;
}

void Fft::prepare (int newOrder)
{
    order = newOrder < 0 ? 0 : newOrder;
    size = 1 << order;

    bitReversal.assign (static_cast<size_t> (size), 0);
    for (int index = 0; index < size; ++index)
    {
        unsigned int value = static_cast<unsigned int> (index);
        unsigned int reversed = 0;
        for (int bit = 0; bit < order; ++bit)
        {
            reversed = (reversed << 1) | (value & 1u);
            value >>= 1;
        }
        bitReversal[static_cast<size_t> (index)] = static_cast<int> (reversed);
    }

    twiddles.assign (static_cast<size_t> (size / 2), std::complex<float> (1.0f, 0.0f));
    for (int k = 0; k < size / 2; ++k)
    {
        const double angle = -twoPi * static_cast<double> (k) / static_cast<double> (size);
        twiddles[static_cast<size_t> (k)] =
            std::complex<float> (static_cast<float> (std::cos (angle)),
                                 static_cast<float> (std::sin (angle)));
    }
}

void Fft::transform (std::complex<float>* data) const noexcept
{
    if (size <= 1)
        return;

    for (int index = 0; index < size; ++index)
    {
        const int reversed = bitReversal[static_cast<size_t> (index)];
        if (index < reversed)
            std::swap (data[index], data[reversed]);
    }

    for (int length = 2; length <= size; length <<= 1)
    {
        const int half = length >> 1;
        const int step = size / length;

        for (int start = 0; start < size; start += length)
        {
            for (int offset = 0; offset < half; ++offset)
            {
                const auto w = twiddles[static_cast<size_t> (offset * step)];
                const auto u = data[start + offset];
                const auto v = data[start + offset + half] * w;
                data[start + offset] = u + v;
                data[start + offset + half] = u - v;
            }
        }
    }
}

void Fft::forward (std::complex<float>* data) const noexcept
{
    transform (data);
}

void Fft::inverse (std::complex<float>* data) const noexcept
{
    if (size <= 1)
        return;

    for (int index = 0; index < size; ++index)
        data[index] = std::conj (data[index]);

    transform (data);

    const float scale = 1.0f / static_cast<float> (size);
    for (int index = 0; index < size; ++index)
        data[index] = std::conj (data[index]) * scale;
}

} // namespace PodcastCleaner
