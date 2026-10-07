#include "Analysis/LoudnessMeter.h"
#include "Analysis/ProfanityDetector.h"
#include "Analysis/VoiceActivityDetector.h"
#include "DSPStages/CensorStage.h"
#include "DSPStages/NonSpeechSilencer.h"
#include "DSPStages/SpectralDenoiser.h"
#include "DSPStages/TruePeakLimiter.h"
#include "DSPUtils/Fft.h"
#include "DSPUtils/Stft.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace PodcastCleaner;

namespace
{
int failures = 0;

void check (bool condition, const char* what)
{
    if (! condition)
    {
        std::printf ("FAIL: %s\n", what);
        ++failures;
    }
}

float maxAbsError (const std::vector<float>& a, const std::vector<float>& b)
{
    float worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));
    return worst;
}

float rms (const std::vector<float>& v, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += static_cast<double> (v[static_cast<size_t> (i)]) * v[static_cast<size_t> (i)];
    return static_cast<float> (std::sqrt (sum / (to - from)));
}

constexpr double twoPi = 6.283185307179586;
}

int main()
{
    std::mt19937 rng (12345);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    // FFT round trip.
    {
        Fft fft;
        fft.prepare (10);
        std::vector<std::complex<float>> data (1024), original (1024);
        for (int i = 0; i < 1024; ++i) data[static_cast<size_t> (i)] = { dist (rng), dist (rng) };
        original = data;
        fft.forward (data.data());
        fft.inverse (data.data());
        float err = 0.0f;
        for (int i = 0; i < 1024; ++i)
            err = std::max (err, std::abs (data[static_cast<size_t> (i)] - original[static_cast<size_t> (i)]));
        check (err < 1.0e-4f, "fft round-trip");
    }

    // STFT round trip and streaming identity.
    {
        Stft stft;
        stft.prepare (10, 4);
        std::vector<float> signal (5003);
        for (auto& s : signal) s = dist (rng);
        std::vector<SpectralFrame> frames;
        stft.analyze (signal, frames);
        std::vector<float> reconstructed;
        stft.synthesize (frames, reconstructed, static_cast<int> (signal.size()));
        check (maxAbsError (signal, reconstructed) < 1.0e-4f, "stft analyze/synthesize");
        std::vector<float> streamed;
        stft.process (signal, nullptr, streamed);
        check (streamed.size() == signal.size(), "stft process length");
        check (maxAbsError (signal, streamed) < 1.0e-4f, "stft process identity");
    }

    // Profanity detection.
    {
        ProfanityDetector detector;
        check (detector.isProfane ("хуй"), "profanity хуй");
        check (detector.isProfane ("xуй"), "profanity obfuscated xуй");
        check (detector.isProfane ("блядь"), "profanity блядь");
        check (detector.isProfane ("пиздец"), "profanity пиздец");
        check (detector.isProfane ("БЛЯЯЯДЬ"), "profanity repeated letters");
        check (detector.isProfane ("FUCKING"), "profanity FUCKING");
        check (! detector.isProfane ("хлеб"), "no false positive хлеб");
        check (! detector.isProfane ("class"), "no false positive class");
        check (! detector.isProfane ("Херсон"), "no false positive Херсон");
        check (ProfanityDetector::normalise ("ПиздЕЦ!") == "пиздец", "profanity normalise");

        const std::vector<TranscriptWord> words = {
            { " привет", 0, 500, 0.9f }, { " блядь", 600, 900, 0.8f }, { " нахуй", 1400, 1700, 0.8f } };
        const auto ranges = detector.findRanges (words, 1000, 5000, 50);
        check (ranges.size() == 2 && ranges[0].start == 550 && ranges[0].end == 950,
               "profanity ranges");
    }
    // VAD: the noise floor must not be inflated by loud speech, and a speech-like
    // frame must look more like speech than a flat noise frame.
    {
        VoiceActivityDetector vad;
        vad.prepare (513);
        std::vector<float> floor (513, 0.01f);
        float floorProbability = 0.0f;
        for (int i = 0; i < 30; ++i)
            floorProbability = vad.analyseFrame (floor.data(), 513);
        float floorEstimate = *std::max_element (vad.getNoiseMagnitude().begin(),
                                                 vad.getNoiseMagnitude().end());

        std::vector<float> speech (513, 0.01f);
        for (int bin = 0; bin < 513; bin += 8)
            speech[static_cast<size_t> (bin)] = 0.3f;
        float speechProbability = 0.0f;
        for (int i = 0; i < 30; ++i)
            speechProbability = vad.analyseFrame (speech.data(), 513);
        float speechEstimate = *std::max_element (vad.getNoiseMagnitude().begin(),
                                                  vad.getNoiseMagnitude().end());

        std::printf ("vad: floor p=%.3f estimate=%.4f; speech p=%.3f estimate=%.4f\n",
                     floorProbability, floorEstimate, speechProbability, speechEstimate);
        check (speechEstimate < floorEstimate * 3.0f, "vad floor not inflated by speech");
        check (speechProbability > floorProbability, "vad prefers speech-like frames");
    }

    // Denoiser: strongly reduce noise while leaving speech essentially untouched.
    {
        const double sr = 16000.0;
        std::normal_distribution<float> noise (0.0f, 0.02f);
        std::vector<float> input (16000);
        for (int i = 0; i < 16000; ++i)
        {
            const double t = i / sr;
            float speech = 0.0f;
            if (t >= 0.3 && t < 0.8)
                speech = 0.30f * std::sin (twoPi * 180.0 * t)
                       + 0.18f * std::sin (twoPi * 360.0 * t)
                       + 0.10f * std::sin (twoPi * 900.0 * t);
            input[static_cast<size_t> (i)] = speech + noise (rng);
        }

        SpectralDenoiser denoiser;
        denoiser.prepare (sr);
        std::vector<float> output;
        denoiser.process (input, output);

        const float noiseIn = rms (input, 200, 3000);
        const float noiseOut = rms (output, 200, 3000);
        const float speechIn = rms (input, 8000, 12000);
        const float speechOut = rms (output, 8000, 12000);
        std::printf ("denoiser: noise %.4f->%.4f (%.2fx); speech %.4f->%.4f (%.3f)\n",
                     noiseIn, noiseOut, noiseOut / noiseIn, speechIn, speechOut, speechOut / speechIn);
        check (output.size() == input.size(), "denoiser length");
        check (noiseOut < noiseIn * 0.6f, "denoiser reduces noise");
        check (speechOut > speechIn * 0.9f, "denoiser preserves speech");
    }

    // Non-speech silencer: gate non-speech to silence, keep speech, fade smoothly.
    {
        const double sr = 16000.0;
        const int length = 48000;   // 3 seconds
        std::normal_distribution<float> noise (0.0f, 0.02f);
        std::vector<float> input (length);
        for (int i = 0; i < length; ++i)
        {
            const double t = i / sr;
            float speech = 0.0f;
            if (t >= 0.4 && t < 1.6)   // speech burst: samples 6400..25600
                speech = 0.30f * std::sin (twoPi * 180.0 * t)
                       + 0.18f * std::sin (twoPi * 360.0 * t)
                       + 0.10f * std::sin (twoPi * 900.0 * t);
            input[static_cast<size_t> (i)] = speech + noise (rng);
        }

        NonSpeechSilencer silencer;
        NonSpeechSilencer::Settings settings;
        silencer.prepare (sr, settings);

        std::vector<float> envelope;
        silencer.buildEnvelope (input, envelope);
        check (envelope.size() == input.size(), "silencer envelope length");

        float preSpeech = 0.0f;
        for (int i = 2000; i < 5000; ++i) preSpeech = std::max (preSpeech, envelope[static_cast<size_t> (i)]);
        float postSpeech = 0.0f;
        for (int i = 45000; i < 47500; ++i) postSpeech = std::max (postSpeech, envelope[static_cast<size_t> (i)]);
        float speechMin = 1.0f;
        for (int i = 10000; i < 20000; ++i) speechMin = std::min (speechMin, envelope[static_cast<size_t> (i)]);

        bool fading = false;
        for (int i = 6400; i < 7000; ++i)
        {
            const float g = envelope[static_cast<size_t> (i)];
            if (g > 0.1f && g < 0.9f) fading = true;
        }

        std::printf ("silencer: pre %.4f post %.4f speech %.4f ratio %.3f\n",
                     preSpeech, postSpeech, speechMin, silencer.getSpeechRatio());
        check (preSpeech < 0.05f, "silencer silences leading non-speech");
        check (postSpeech < 0.05f, "silencer silences trailing non-speech");
        check (speechMin > 0.9f, "silencer keeps speech open");
        check (fading, "silencer fades smoothly at the boundary");

        std::vector<float> output;
        silencer.process (input, output);
        check (output.size() == input.size(), "silencer process length");
        const float speechIn = rms (input, 10000, 20000);
        const float speechOut = rms (output, 10000, 20000);
        const float silenceOut = rms (output, 45000, 47500);
        check (speechOut > speechIn * 0.9f, "silencer preserves speech samples");
        check (silenceOut < 0.001f, "silencer output is silent off-speech");
    }

    // Censor: mute and beep both preserve the length.
    {
        CensorStage censor;
        censor.prepare (1000.0);

        std::vector<float> muted (4000, 0.5f);
        censor.setMode (CensorMode::Mute);
        censor.apply (muted, { SampleRange { 1000, 3000 } });
        check (muted.size() == 4000, "censor mute length");
        check (std::abs (muted[2000]) < 1.0e-4f, "censor mutes");
        check (std::abs (muted[500]) > 0.4f, "censor keeps surroundings");

        std::vector<float> beeped (4000, 0.5f);
        censor.setMode (CensorMode::Beep);
        censor.setBeepFrequency (1000.0);
        censor.apply (beeped, { SampleRange { 1000, 3000 } });
        check (beeped.size() == 4000, "censor beep length");
        check (std::abs (beeped[500] - 0.5f) < 1.0e-4f, "censor beep keeps surroundings");
    }

    // Limiter ceiling.
    {
        std::vector<float> channel (8000);
        for (int i = 0; i < 8000; ++i)
            channel[static_cast<size_t> (i)] = 2.0f * std::sin (twoPi * 220.0 * i / 44100.0);
        TruePeakLimiter limiter;
        limiter.prepare (44100.0);
        limiter.setCeilingDb (-1.0f);
        limiter.process (channel);
        float peak = 0.0f;
        for (float s : channel) peak = std::max (peak, std::abs (s));
        check (peak < std::pow (10.0f, -1.0f / 20.0f) + 1.0e-4f, "limiter ceiling");
    }

    // Loudness.
    {
        std::vector<std::vector<float>> stereo (2, std::vector<float> (144000));
        for (auto& ch : stereo)
            for (size_t i = 0; i < ch.size(); ++i)
                ch[i] = 0.1f * std::sin (twoPi * 1000.0 * i / 48000.0);
        const float lufs = LoudnessMeter::integratedLoudness (stereo, 48000.0);
        check (lufs > -21.5f && lufs < -19.0f, "loudness magnitude");
    }

    std::printf (failures == 0 ? "ALL CORE TESTS PASSED\n" : "%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
