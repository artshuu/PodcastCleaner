#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PodcastCleaner
{

/** Decoded audio: de-interleaved float channels, samples in roughly [-1, 1]. */
struct AudioData
{
    int sampleRate = 44100;
    std::vector<std::vector<float>> channels;

    std::int64_t getNumSamples() const
    {
        return channels.empty() ? 0 : static_cast<std::int64_t> (channels.front().size());
    }
};

/** Reads a RIFF/WAVE file (PCM 16/24/32-bit or IEEE float 32/64-bit). */
bool readWav (const std::string& path, AudioData& out, std::string& error);

/** Writes a RIFF/WAVE file. bitDepth may be 16 (PCM) or 32 (IEEE float). */
bool writeWav (const std::string& path, const AudioData& data, int bitDepth, std::string& error);

} // namespace PodcastCleaner
