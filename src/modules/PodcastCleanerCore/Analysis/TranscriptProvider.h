#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PodcastCleaner
{

/** A single recognised word with its time span.

    Times are expressed in milliseconds relative to the start of the processed
    audio (the same timeline the audio samples use). */
struct TranscriptWord
{
    std::string text;
    std::int64_t startMs = 0;
    std::int64_t endMs = 0;
    float probability = 0.0f;
};

/** A half-open range of samples [start, end). */
struct SampleRange
{
    std::int64_t start = 0;
    std::int64_t end = 0;
};

/** Abstract speech-to-text backend.

    Implementations turn mono PCM into a word list with timecodes. The offline
    pipeline uses the ids to build a censoring map; alternative backends
    (Whisper, cloud APIs, pre-computed JSON) only need to implement this
    interface. */
class TranscriptProvider
{
public:
    virtual ~TranscriptProvider() = default;

    /** Transcribes the given mono samples (range roughly [-1, 1]). Returns an
        empty vector when nothing was recognised. */
    virtual std::vector<TranscriptWord> transcribe (const std::vector<float>& monoSamples,
                                                    int sampleRate) = 0;

    /** Human-readable backend name, used in reports. */
    virtual std::string getName() const = 0;
};

} // namespace PodcastCleaner
