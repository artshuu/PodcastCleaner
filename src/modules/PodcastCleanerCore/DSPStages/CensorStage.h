#pragma once

#include "Analysis/TranscriptProvider.h"

#include <vector>

namespace PodcastCleaner
{

/** Modes used to conceal a flagged word. */
enum class CensorMode
{
    Mute, // silence the range
    Beep  // replace the range with a tone
};

/** Sample-accurate word censor.

    Given ranges of samples it mutes them or replaces them with a sine beep,
    applying short fades at the edges so no clicks are introduced. The number of
    samples is never changed, so the track duration is preserved. */
class CensorStage
{
public:
    void prepare (double newSampleRate) noexcept;

    void setMode (CensorMode newMode) noexcept { mode = newMode; }
    CensorMode getMode() const noexcept { return mode; }

    void setBeepFrequency (double hertz) noexcept { beepFrequency = hertz; }
    double getBeepFrequency() const noexcept { return beepFrequency; }

    /** Beep level relative to the local speech level (dB). */
    void setBeepGainDb (float decibels) noexcept { beepGainDb = decibels; }

    void setFadeSeconds (double seconds) noexcept { fadeSeconds = seconds; }

    /** Applies the censor to a single channel in place. */
    void apply (std::vector<float>& channel, const std::vector<SampleRange>& ranges) const;

private:
    double sampleRate = 44100.0;
    CensorMode mode = CensorMode::Mute;
    double beepFrequency = 1000.0;
    float beepGainDb = 0.0f;
    double fadeSeconds = 0.005;
};

} // namespace PodcastCleaner
