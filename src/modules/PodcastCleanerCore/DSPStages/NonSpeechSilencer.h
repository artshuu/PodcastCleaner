#pragma once

#include "Analysis/VoiceActivityDetector.h"
#include "DSPUtils/Decibels.h"
#include "DSPUtils/Stft.h"

#include <vector>

namespace PodcastCleaner
{

/** Replaces non-speech regions with (near) silence, cross-fading at every
    boundary so the transitions never click.

    A VoiceActivityDetector runs over the analysis spectra of a mono mixdown and
    returns a per-frame speech probability. That probability drives a hang-over
    gate (open once speech is detected, stay open for holdMs after it ends) whose
    0/1 target is smoothed by an asymmetric one-pole attack/release envelope and
    then linearly interpolated to a per-sample gain. The envelope is multiplied
    onto every channel, so speech regions keep unity gain (they pass through
    untouched) while non-speech regions fall to the configured floor.

    The output always has exactly as many samples as the input - the duration
    never changes. */
class NonSpeechSilencer
{
public:
    struct Settings
    {
        int fftOrder = 10;              // window length = 1 << fftOrder
        int hopDivisor = 4;             // 75% overlap
        float sensitivity = 0.5f;       // VAD sensitivity (0..1)
        float openThreshold = 0.35f;    // speech probability that opens the gate
        float attackMs = 30.0f;         // fade-in once speech appears
        float releaseMs = 150.0f;       // fade-out after speech ends
        float holdMs = 150.0f;          // keep the gate open this long after speech
        float floorDb = -60.0f;         // gain applied to non-speech (silence)

        /** Linear gain applied to non-speech bins. */
        float gainFloor() const noexcept { return decibelsToGain (floorDb); }
    };

    NonSpeechSilencer() = default;

    void prepare (double sampleRate);
    void prepare (double sampleRate, const Settings& newSettings);

    /** Builds the per-sample gate envelope from a mono signal (usually the
        mixdown of all channels). Resets the detector, so it must be called once
        per file before applyEnvelope(). */
    void buildEnvelope (const std::vector<float>& mono, std::vector<float>& envelope);

    /** Multiplies a channel in place by an envelope produced by buildEnvelope(). */
    static void applyEnvelope (std::vector<float>& channel, const std::vector<float>& envelope);

    /** Convenience for mono buffers: buildEnvelope() + applyEnvelope(). */
    void process (const std::vector<float>& input, std::vector<float>& output);

    /** Fraction of frames classified as speech during the last buildEnvelope(). */
    float getSpeechRatio() const noexcept { return speechRatio; }

private:
    Stft stft;
    VoiceActivityDetector vad;
    Settings settings;
    double sampleRate = 44100.0;
    float speechRatio = 1.0f;
};

} // namespace PodcastCleaner
