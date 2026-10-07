#pragma once

#include "Analysis/ProfanityDetector.h"
#include "Analysis/TranscriptProvider.h"
#include "DSPStages/CensorStage.h"
#include "DSPStages/NonSpeechSilencer.h"
#include "DSPStages/SpectralDenoiser.h"
#include "WavFile.h"

#include <vector>

namespace PodcastCleaner
{

/** Configuration for a single offline pass. */
struct PipelineOptions
{
    bool denoise = true;
    SpectralDenoiser::Settings denoiseSettings;

    bool silenceNonSpeech = false;
    NonSpeechSilencer::Settings silenceSettings;

    bool censor = false;
    CensorMode censorMode = CensorMode::Mute;
    double beepFrequency = 1000.0;
    float beepGainDb = 0.0f;
    std::int64_t censorMarginMs = 40;

    bool mastering = true;
    float targetLufs = -16.0f;
    float ceilingDb = -1.0f;

    /** Ranges to censor regardless of the transcript (manual mode). */
    std::vector<SampleRange> forcedRanges;
};

/** Measurements collected while processing. */
struct PipelineReport
{
    std::int64_t inputSamples = 0;
    std::int64_t outputSamples = 0;

    float inputLufs = 0.0f;
    float finalLufs = 0.0f;
    float inputTruePeakDb = 0.0f;
    float outputTruePeakDb = 0.0f;
    float appliedGainDb = 0.0f;
    float limiterReductionDb = 0.0f;
    float nonSpeechRatio = 0.0f;
    int denoisedChannels = 0;
    float silencedRatio = 0.0f;   // fraction of samples gated to silence

    std::vector<TranscriptWord> transcript;
    std::vector<SampleRange> censoredRanges;
};

/** Runs the full offline cleanup chain on decoded audio, in place.

    Order: spectral denoise -> silence non-speech -> (speech recognition ->)
    profanity censor -> loudness normalisation -> true-peak limiting. The sample count of every
    channel is identical before and after, so the track duration never changes. */
class OfflinePipeline
{
public:
    /** transcriptProvider may be null (manual censoring / no censoring). */
    PipelineReport run (AudioData& audio,
                        const PipelineOptions& options,
                        TranscriptProvider* transcriptProvider) const;
};

} // namespace PodcastCleaner
