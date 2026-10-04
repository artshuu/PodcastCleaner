#include "WhisperTranscriptProvider.h"

#include "DSPUtils/Resampler.h"

#include <whisper.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>

namespace PodcastCleaner
{

namespace
{
constexpr int whisperSampleRate = 16000;
constexpr int centisecondsToMs = 10; // whisper timecodes are in 10 ms units
}

struct WhisperTranscriptProvider::Impl
{
    whisper_context* context = nullptr;
};

WhisperTranscriptProvider::WhisperTranscriptProvider (Options newOptions)
    : impl (std::make_unique<Impl>()), options (std::move (newOptions))
{
    whisper_context_params contextParams = whisper_context_default_params();
    contextParams.use_gpu = false;
    impl->context = whisper_init_from_file_with_params (options.modelPath.c_str(), contextParams);
}

WhisperTranscriptProvider::~WhisperTranscriptProvider()
{
    if (impl && impl->context != nullptr)
        whisper_free (impl->context);
}

bool WhisperTranscriptProvider::isReady() const
{
    return impl && impl->context != nullptr;
}

std::string WhisperTranscriptProvider::getName() const
{
    return "whisper.cpp";
}

std::string WhisperTranscriptProvider::getDescription() const
{
    std::string description = "whisper.cpp";
    description += options.beamSize > 1
        ? " (beam=" + std::to_string (options.beamSize) + ")"
        : " (greedy)";
    if (options.useVad && ! options.vadModelPath.empty())
        description += ", silero-vad";
    return description;
}

static bool useVoiceActivity (const WhisperTranscriptProvider::Options& options)
{
    return options.useVad && ! options.vadModelPath.empty();
}

std::vector<TranscriptWord> WhisperTranscriptProvider::transcribe (const std::vector<float>& monoSamples,
                                                                   int sampleRate)
{
    std::vector<TranscriptWord> words;
    if (! isReady() || monoSamples.empty() || sampleRate <= 0)
        return words;

    const std::vector<float> pcm = resampleLinearPhase (monoSamples, sampleRate, whisperSampleRate);
    if (pcm.empty())
        return words;

    const bool beam = options.beamSize > 1;
    whisper_full_params params = whisper_full_default_params (
        beam ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);

    params.print_realtime = false;
    params.print_progress = false;
    params.print_timestamps = false;
    params.print_special = false;
    params.translate = options.translate;
    params.token_timestamps = true;
    params.no_context = true;
    params.suppress_blank = true;
    params.suppress_nst = options.suppressNonSpeech;
    params.temperature = 0.0f;
    params.temperature_inc = 0.4f; // fall back further when a decode looks bad
    params.n_threads = options.threads > 0 ? options.threads
        : static_cast<int> (std::max (1u, std::thread::hardware_concurrency()));

    if (beam)
        params.beam_search.beam_size = options.beamSize;
    else
        params.greedy.best_of = 5;

    if (useVoiceActivity (options))
    {
        params.vad = true;
        params.vad_model_path = options.vadModelPath.c_str();
        params.vad_params.speech_pad_ms = 60; // keep word onsets/offsets intact
    }

    const bool autoDetect = options.language.empty() || options.language == "auto";
    params.language = autoDetect ? nullptr : options.language.c_str();
    params.detect_language = autoDetect;

    if (whisper_full (impl->context, params, pcm.data(), static_cast<int> (pcm.size())) != 0)
        return words;

    const int segmentCount = whisper_full_n_segments (impl->context);
    std::string current;
    std::int64_t startMs = 0;
    std::int64_t endMs = 0;
    double probabilitySum = 0.0;
    int probabilityCount = 0;
    bool inWord = false;

    const auto flushWord = [&]()
    {
        if (inWord && ! current.empty())
        {
            TranscriptWord word;
            word.text = current;
            word.startMs = startMs;
            word.endMs = endMs;
            word.probability = probabilityCount > 0
                ? static_cast<float> (probabilitySum / probabilityCount) : 0.0f;
            words.push_back (word);
        }
        current.clear();
        inWord = false;
        probabilitySum = 0.0;
        probabilityCount = 0;
    };

    for (int segment = 0; segment < segmentCount; ++segment)
    {
        const int tokenCount = whisper_full_n_tokens (impl->context, segment);
        for (int token = 0; token < tokenCount; ++token)
        {
            if (whisper_full_get_token_id (impl->context, segment, token)
                    >= whisper_token_eot (impl->context))
                continue;

            const char* tokenText = whisper_full_get_token_text (impl->context, segment, token);
            if (tokenText == nullptr)
                continue;

            const std::string text (tokenText);
            if (text.empty())
                continue;

            const bool beginsWord = text.front() == ' ' || text.front() == '\n' || ! inWord;
            if (beginsWord)
                flushWord();

            size_t offset = 0;
            while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\n'))
                ++offset;

            current += text.substr (offset);

            // VAD-safe timecodes: whisper_full_get_token_t0/t1 are mapped back to
            // the original timeline even when the internal VAD removed silence.
            if (! inWord)
            {
                startMs = whisper_full_get_token_t0 (impl->context, segment, token)
                        * centisecondsToMs;
                inWord = true;
            }
            endMs = whisper_full_get_token_t1 (impl->context, segment, token) * centisecondsToMs;
            probabilitySum += whisper_full_get_token_p (impl->context, segment, token);
            ++probabilityCount;
        }
    }
    flushWord();

    return words;
}

} // namespace PodcastCleaner
