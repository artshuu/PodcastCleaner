#include "WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace PodcastCleaner
{

namespace
{

std::uint16_t readU16 (const unsigned char* p) { return static_cast<std::uint16_t> (p[0] | (p[1] << 8)); }
std::uint32_t readU32 (const unsigned char* p)
{
    return static_cast<std::uint32_t> (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

float sampleToFloat (const unsigned char* p, int bits, bool isFloat)
{
    if (isFloat)
    {
        if (bits == 32)
        {
            std::uint32_t raw = readU32 (p);
            float value;
            std::memcpy (&value, &raw, sizeof (float));
            return value;
        }
        std::uint64_t raw = 0;
        for (int i = 0; i < 8; ++i)
            raw |= static_cast<std::uint64_t> (p[i]) << (8 * i);
        double value;
        std::memcpy (&value, &raw, sizeof (double));
        return static_cast<float> (value);
    }

    if (bits == 16)
    {
        const std::int16_t v = static_cast<std::int16_t> (readU16 (p));
        return static_cast<float> (v) / 32768.0f;
    }
    if (bits == 24)
    {
        std::int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
        if (v & 0x00800000)
            v |= ~0x00FFFFFF;
        return static_cast<float> (v) / 8388608.0f;
    }
    if (bits == 32)
    {
        const std::int32_t v = static_cast<std::int32_t> (readU32 (p));
        return static_cast<float> (v) / 2147483648.0f;
    }
    return 0.0f;
}

void writeU16 (std::ofstream& out, std::uint16_t value)
{
    const unsigned char bytes[2] { static_cast<unsigned char> (value & 0xFF),
                                   static_cast<unsigned char> ((value >> 8) & 0xFF) };
    out.write (reinterpret_cast<const char*> (bytes), 2);
}

void writeU32 (std::ofstream& out, std::uint32_t value)
{
    const unsigned char bytes[4] { static_cast<unsigned char> (value & 0xFF),
                                   static_cast<unsigned char> ((value >> 8) & 0xFF),
                                   static_cast<unsigned char> ((value >> 16) & 0xFF),
                                   static_cast<unsigned char> ((value >> 24) & 0xFF) };
    out.write (reinterpret_cast<const char*> (bytes), 4);
}

} // namespace

bool readWav (const std::string& path, AudioData& out, std::string& error)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        error = "cannot open input file: " + path;
        return false;
    }

    char riff[4];
    in.read (riff, 4);
    unsigned char header[4];
    in.read (reinterpret_cast<char*> (header), 4); // RIFF size
    char wave[4];
    in.read (wave, 4);
    if (std::memcmp (riff, "RIFF", 4) != 0 || std::memcmp (wave, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    int numChannels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    int blockAlign = 0;
    std::uint16_t formatTag = 0;
    bool isFloat = false;
    std::vector<unsigned char> dataBlock;

    while (in)
    {
        char chunkId[4];
        unsigned char sizeBytes[4];
        in.read (chunkId, 4);
        if (in.gcount() != 4)
            break;
        in.read (reinterpret_cast<char*> (sizeBytes), 4);
        const std::uint32_t chunkSize = readU32 (sizeBytes);

        if (std::memcmp (chunkId, "fmt ", 4) == 0)
        {
            std::vector<unsigned char> fmt (chunkSize);
            in.read (reinterpret_cast<char*> (fmt.data()), chunkSize);
            if (fmt.size() < 16) { error = "invalid fmt chunk"; return false; }
            formatTag = readU16 (fmt.data());
            numChannels = readU16 (fmt.data() + 2);
            sampleRate = static_cast<int> (readU32 (fmt.data() + 4));
            blockAlign = readU16 (fmt.data() + 12);
            bitsPerSample = readU16 (fmt.data() + 14);
            if (formatTag == 0xFFFE && fmt.size() >= 26)
                formatTag = readU16 (fmt.data() + 24); // sub-format
            isFloat = (formatTag == 3);
        }
        else if (std::memcmp (chunkId, "data", 4) == 0)
        {
            dataBlock.resize (chunkSize);
            in.read (reinterpret_cast<char*> (dataBlock.data()), chunkSize);
        }
        else
        {
            in.seekg (chunkSize + (chunkSize & 1), std::ios::cur);
        }
    }

    if (numChannels <= 0 || sampleRate <= 0 || bitsPerSample <= 0 || dataBlock.empty())
    {
        error = "incomplete WAV (missing fmt/data)";
        return false;
    }

    const int bytesPerSample = bitsPerSample / 8;
    if (blockAlign <= 0)
        blockAlign = numChannels * bytesPerSample;
    const std::int64_t frameCount =
        static_cast<std::int64_t> (dataBlock.size() / static_cast<size_t> (blockAlign));

    out.sampleRate = sampleRate;
    out.channels.assign (static_cast<size_t> (numChannels),
                         std::vector<float> (static_cast<size_t> (frameCount), 0.0f));

    for (std::int64_t frame = 0; frame < frameCount; ++frame)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const size_t offset = static_cast<size_t> (frame * blockAlign + channel * bytesPerSample);
            out.channels[static_cast<size_t> (channel)][static_cast<size_t> (frame)] =
                sampleToFloat (dataBlock.data() + offset, bitsPerSample, isFloat);
        }
    }
    return true;
}

bool writeWav (const std::string& path, const AudioData& data, int bitDepth, std::string& error)
{
    if (data.channels.empty())
    {
        error = "no audio to write";
        return false;
    }

    const int numChannels = static_cast<int> (data.channels.size());
    const std::int64_t frameCount = data.getNumSamples();
    const bool isFloat = (bitDepth == 32);
    const int bytesPerSample = bitDepth / 8;
    const int blockAlign = numChannels * bytesPerSample;
    const std::uint32_t dataSize = static_cast<std::uint32_t> (frameCount * blockAlign);

    std::ofstream out (path, std::ios::binary);
    if (! out)
    {
        error = "cannot open output file: " + path;
        return false;
    }

    out.write ("RIFF", 4);
    writeU32 (out, 36u + dataSize);
    out.write ("WAVE", 4);
    out.write ("fmt ", 4);
    writeU32 (out, 16u);
    writeU16 (out, static_cast<std::uint16_t> (isFloat ? 3 : 1));
    writeU16 (out, static_cast<std::uint16_t> (numChannels));
    writeU32 (out, static_cast<std::uint32_t> (data.sampleRate));
    writeU32 (out, static_cast<std::uint32_t> (data.sampleRate * blockAlign));
    writeU16 (out, static_cast<std::uint16_t> (blockAlign));
    writeU16 (out, static_cast<std::uint16_t> (bitDepth));
    out.write ("data", 4);
    writeU32 (out, dataSize);

    for (std::int64_t frame = 0; frame < frameCount; ++frame)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float value = data.channels[static_cast<size_t> (channel)][static_cast<size_t> (frame)];
            if (isFloat)
            {
                const float clamped = std::min (1.0f, std::max (-1.0f, value));
                std::uint32_t raw;
                std::memcpy (&raw, &clamped, sizeof (float));
                writeU32 (out, raw);
            }
            else
            {
                const float clamped = std::min (1.0f, std::max (-1.0f, value));
                const std::int32_t scaled =
                    static_cast<std::int32_t> (std::lround (clamped * 32767.0f));
                writeU16 (out, static_cast<std::uint16_t> (static_cast<std::int16_t> (scaled)));
            }
        }
    }

    return static_cast<bool> (out);
}

} // namespace PodcastCleaner
