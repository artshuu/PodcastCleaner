#pragma once

#include "Analysis/TranscriptProvider.h"

#include <memory>
#include <string>

namespace PodcastCleaner
{

/** Whisper.cpp speech-to-text backend.

    Loads a ggml Whisper model from disk and transcribes mono PCM into words
    with word-level timecodes. The Whisper API is hidden behind a pimpl so that
    dependents do not need to see or link whisper.h directly.

    Recognition quality options:
      - beam search (instead of greedy) for noticeably better transcripts,
      - an optional Silero VAD model, which removes silence/music before the
        model sees it and therefore suppresses hallucinated text,
      - suppression of non-speech tokens.

    Word timecodes are always mapped back to the original (un-VAD-ed) timeline,
    so they stay aligned with the audio the pipeline censors. */
class WhisperTranscriptProvider final : public TranscriptProvider
{
public:
    struct Options
    {
        std::string modelPath;
        std::string vadModelPath;      // Silero ggml VAD model; empty disables VAD
        std::string language = "auto"; // "ru", "en", "auto", ...
        int threads = 0;               // 0 = hardware concurrency
        bool translate = false;        // translate to English instead of transcribing
        int beamSize = 5;              // 1 selects greedy decoding
        bool useVad = true;            // honoured only when vadModelPath is set
        bool suppressNonSpeech = true; // drop musical symbols / punctuation-only tokens
    };

    explicit WhisperTranscriptProvider (Options options);
    ~WhisperTranscriptProvider() override;

    std::vector<TranscriptWord> transcribe (const std::vector<float>& monoSamples,
                                            int sampleRate) override;

    std::string getName() const override;

    /** Human-readable summary of the active decoding settings. */
    std::string getDescription() const;

    /** True when the model was loaded successfully. */
    bool isReady() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Options options;
};

} // namespace PodcastCleaner
