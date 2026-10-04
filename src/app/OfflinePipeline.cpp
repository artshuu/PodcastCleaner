#include "OfflinePipeline.h"

#include "Analysis/LoudnessMeter.h"
#include "DSPStages/TruePeakLimiter.h"
#include "DSPUtils/Decibels.h"

#include <algorithm>
#include <cmath>

namespace PodcastCleaner
{

namespace
{

std::vector<float> mixdown (const std::vector<std::vector<float>>& channels)
{
    if (channels.empty())
        return {};

    const size_t sampleCount = channels.front().size();
    std::vector<float> mono (sampleCount, 0.0f);
    for (const auto& channel : channels)
        for (size_t index = 0; index < sampleCount; ++index)
            mono[index] += channel[index];

    const float scale = 1.0f / static_cast<float> (channels.size());
    for (auto& sample : mono)
        sample *= scale;
    return mono;
}

std::vector<SampleRange> mergeRanges (std::vector<SampleRange> ranges, std::int64_t totalSamples)
{
    for (auto& range : ranges)
    {
        range.start = std::min (std::max<std::int64_t> (0, range.start), totalSamples);
        range.end = std::min (std::max<std::int64_t> (0, range.end), totalSamples);
    }

    std::sort (ranges.begin(), ranges.end(),
               [] (const SampleRange& a, const SampleRange& b) { return a.start < b.start; });

    std::vector<SampleRange> merged;
    for (const auto& range : ranges)
    {
        if (range.end <= range.start)
            continue;
        if (! merged.empty() && range.start <= merged.back().end)
            merged.back().end = std::max (merged.back().end, range.end);
        else
            merged.push_back (range);
    }
    return merged;
}

} // namespace

PipelineReport OfflinePipeline::run (AudioData& audio,
                                     const PipelineOptions& options,
                                     TranscriptProvider* transcriptProvider) const
{
    PipelineReport report;
    report.inputSamples = audio.getNumSamples();
    if (audio.channels.empty() || audio.getNumSamples() == 0)
    {
        report.outputSamples = 0;
        return report;
    }

    const double sampleRate = audio.sampleRate;
    const std::int64_t totalSamples = audio.getNumSamples();

    report.inputLufs = LoudnessMeter::integratedLoudness (audio.channels, sampleRate);
    report.inputTruePeakDb = LoudnessMeter::truePeakDb (audio.channels);

    // 1. Spectral denoise (length preserving).
    if (options.denoise)
    {
        SpectralDenoiser denoiser;
        denoiser.prepare (sampleRate, options.denoiseSettings);
        float ratioSum = 0.0f;
        for (auto& channel : audio.channels)
        {
            std::vector<float> processed;
            denoiser.process (channel, processed);
            channel = std::move (processed);
            ratioSum += denoiser.getNonSpeechRatio();
        }
        report.nonSpeechRatio = ratioSum / static_cast<float> (audio.channels.size());
        report.denoisedChannels = static_cast<int> (audio.channels.size());
    }

    // 2. Censoring.
    std::vector<SampleRange> ranges = options.forcedRanges;
    if (options.censor)
    {
        if (transcriptProvider != nullptr)
        {
            const std::vector<float> mono = mixdown (audio.channels);
            const auto words = transcriptProvider->transcribe (mono, audio.sampleRate);
            report.transcript = words;

            ProfanityDetector detector;
            const std::int64_t margin =
                (options.censorMarginMs * audio.sampleRate) / 1000;
            const auto found = detector.findRanges (words, audio.sampleRate, totalSamples, margin);
            ranges.insert (ranges.end(), found.begin(), found.end());
        }

        ranges = mergeRanges (std::move (ranges), totalSamples);
        if (! ranges.empty())
        {
            CensorStage censor;
            censor.prepare (sampleRate);
            censor.setMode (options.censorMode);
            censor.setBeepFrequency (options.beepFrequency);
            censor.setBeepGainDb (options.beepGainDb);
            for (auto& channel : audio.channels)
                censor.apply (channel, ranges);
            report.censoredRanges = ranges;
        }
    }

    // 3. Mastering: loudness normalisation then true-peak limiting.
    if (options.mastering)
    {
        const float measured = LoudnessMeter::integratedLoudness (audio.channels, sampleRate);
        float gainDb = options.targetLufs - measured;
        gainDb = std::min (24.0f, std::max (-24.0f, gainDb));
        const float gain = decibelsToGain (gainDb);
        for (auto& channel : audio.channels)
            for (auto& sample : channel)
                sample *= gain;
        report.appliedGainDb = gainDb;

        TruePeakLimiter limiter;
        limiter.prepare (sampleRate);
        limiter.setCeilingDb (options.ceilingDb);
        float worstReduction = 0.0f;
        for (auto& channel : audio.channels)
        {
            limiter.process (channel);
            worstReduction = std::min (worstReduction, limiter.getGainReductionDb());
        }
        report.limiterReductionDb = worstReduction;
    }

    report.finalLufs = LoudnessMeter::integratedLoudness (audio.channels, sampleRate);
    report.outputTruePeakDb = LoudnessMeter::truePeakDb (audio.channels);
    report.outputSamples = audio.getNumSamples();
    return report;
}

} // namespace PodcastCleaner
