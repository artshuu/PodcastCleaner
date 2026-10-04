#include "OfflinePipeline.h"
#include "WavFile.h"

#include "WhisperTranscriptProvider.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace PodcastCleaner;

namespace
{

struct CommandLine
{
    std::string inputPath;
    std::string outputPath;
    PipelineOptions options;
    std::string modelPath;
    std::string vadModelPath;
    std::string language = "auto";
    int threads = 0;
    int beamSize = 5;
    bool useVad = true;
    int bitDepth = 16;
};

void printUsage()
{
    std::cout <<
        "Podcast Cleaner - offline cleanup, censoring and mastering\n\n"
        "Usage:\n"
        "  podcast-cleaner <input.wav> <output.wav> [options]\n\n"
        "Options:\n"
        "  --no-denoise                 disable spectral denoise\n"
        "  --denoise-reduction <dB>     max noise attenuation (default 12)\n"
        "  --speech-protection <0..1>   how strongly speech is preserved (default 0.9)\n"
        "  --vad-sensitivity <0..1>     speech detector sensitivity (default 0.5)\n"
        "  --censor                     enable profanity censoring\n"
        "  --censor-mode mute|beep      how to conceal words (default mute)\n"
        "  --beep-hz <hz>               beep frequency (default 1000)\n"
        "  --beep-gain-db <db>          beep level vs speech (default 0)\n"
        "  --censor-margin-ms <ms>      padding around each word (default 40)\n"
        "  --model <path>               ggml whisper model (enables recognition)\n"
        "  --language <ru|en|auto>      recognition language (default auto)\n"
        "  --threads <n>                whisper threads (default: all cores)\n"
        "  --beam-size <n>              beam search width, 1 = greedy (default 5)\n"
        "  --vad-model <path>           Silero VAD model (default: next to --model)\n"
        "  --no-vad                     skip VAD preprocessing of recognition input\n"
        "  --range <start-end>          manual censor range in ms (repeatable)\n"
        "  --no-mastering               skip loudness normalisation/limiting\n"
        "  --target-lufs <lufs>         loudness target (default -16)\n"
        "  --ceiling-db <db>            true-peak ceiling (default -1)\n"
        "  --bit-depth <16|32>          output bit depth (default 16)\n";
}

bool parseArgs (int argc, char** argv, CommandLine& commandLine)
{
    if (argc < 3)
        return false;

    commandLine.inputPath = argv[1];
    commandLine.outputPath = argv[2];

    const auto nextFloat = [&] (int& index, float& out)
    {
        if (index + 1 >= argc) return false;
        out = std::strtof (argv[++index], nullptr);
        return true;
    };

    for (int index = 3; index < argc; ++index)
    {
        const std::string arg = argv[index];
        if (arg == "--no-denoise") commandLine.options.denoise = false;
        else if (arg == "--censor") commandLine.options.censor = true;
        else if (arg == "--no-mastering") commandLine.options.mastering = false;
        else if (arg == "--denoise-reduction")
            nextFloat (index, commandLine.options.denoiseSettings.reductionDb);
        else if (arg == "--vad-sensitivity")
            nextFloat (index, commandLine.options.denoiseSettings.sensitivity);
        else if (arg == "--beep-hz")
        {
            float value = 1000.0f;
            nextFloat (index, value);
            commandLine.options.beepFrequency = value;
        }
        else if (arg == "--beep-gain-db")
            nextFloat (index, commandLine.options.beepGainDb);
        else if (arg == "--target-lufs")
            nextFloat (index, commandLine.options.targetLufs);
        else if (arg == "--ceiling-db")
            nextFloat (index, commandLine.options.ceilingDb);
        else if (arg == "--censor-mode" && index + 1 < argc)
            commandLine.options.censorMode =
                std::string (argv[++index]) == "beep" ? CensorMode::Beep : CensorMode::Mute;
        else if (arg == "--censor-margin-ms" && index + 1 < argc)
            commandLine.options.censorMarginMs = std::strtoll (argv[++index], nullptr, 10);
        else if (arg == "--model" && index + 1 < argc)
            commandLine.modelPath = argv[++index];
        else if (arg == "--language" && index + 1 < argc)
            commandLine.language = argv[++index];
        else if (arg == "--threads" && index + 1 < argc)
            commandLine.threads = static_cast<int> (std::strtol (argv[++index], nullptr, 10));
        else if (arg == "--beam-size" && index + 1 < argc)
            commandLine.beamSize = static_cast<int> (std::strtol (argv[++index], nullptr, 10));
        else if (arg == "--vad-model" && index + 1 < argc)
            commandLine.vadModelPath = argv[++index];
        else if (arg == "--no-vad")
            commandLine.useVad = false;
        else if (arg == "--speech-protection")
            nextFloat (index, commandLine.options.denoiseSettings.speechProtection);
        else if (arg == "--bit-depth" && index + 1 < argc)
            commandLine.bitDepth = static_cast<int> (std::strtol (argv[++index], nullptr, 10));
        else if (arg == "--range" && index + 1 < argc)
        {
            const std::string spec = argv[++index];
            const auto dash = spec.find ('-');
            if (dash != std::string::npos)
            {
                SampleRange range;
                const double startMs = std::strtod (spec.substr (0, dash).c_str(), nullptr);
                const double endMs = std::strtod (spec.substr (dash + 1).c_str(), nullptr);
                range.start = static_cast<std::int64_t> (startMs);
                range.end = static_cast<std::int64_t> (endMs);
                commandLine.options.forcedRanges.push_back (range); // converted to samples below
            }
        }
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main (int argc, char** argv)
{
    CommandLine commandLine;
    if (! parseArgs (argc, argv, commandLine))
    {
        printUsage();
        return 1;
    }

    AudioData audio;
    std::string error;
    if (! readWav (commandLine.inputPath, audio, error))
    {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }
    std::cout << "Loaded " << audio.channels.size() << " channel(s), "
              << audio.sampleRate << " Hz, " << audio.getNumSamples() << " samples\n";

    // The CLI takes manual ranges in milliseconds; convert to samples now that
    // the sample rate is known.
    for (auto& range : commandLine.options.forcedRanges)
    {
        range.start = range.start * audio.sampleRate / 1000;
        range.end = range.end * audio.sampleRate / 1000;
    }

    std::unique_ptr<WhisperTranscriptProvider> recogniser;
    if (commandLine.options.censor && ! commandLine.modelPath.empty())
    {
        WhisperTranscriptProvider::Options options;
        options.modelPath = commandLine.modelPath;
        options.language = commandLine.language;
        options.threads = commandLine.threads;
        options.beamSize = commandLine.beamSize;
        options.useVad = commandLine.useVad;
        options.vadModelPath = commandLine.vadModelPath;

        // Auto-detect a Silero VAD model shipped next to the Whisper model.
        if (options.useVad && options.vadModelPath.empty())
        {
            const auto slash = commandLine.modelPath.find_last_of ("/\\");
            const std::string directory = slash == std::string::npos
                ? std::string() : commandLine.modelPath.substr (0, slash + 1);
            for (const char* name : { "ggml-silero-v6.2.0.bin", "ggml-silero-v5.1.2.bin" })
            {
                const std::string candidate = directory + name;
                std::ifstream probe (candidate);
                if (probe.good())
                {
                    options.vadModelPath = candidate;
                    break;
                }
            }
        }

        recogniser = std::make_unique<WhisperTranscriptProvider> (options);
        if (! recogniser->isReady())
        {
            std::cerr << "Error: could not load Whisper model '" << commandLine.modelPath << "'\n";
            return 1;
        }
        std::cout << "Speech recognition: " << recogniser->getDescription() << "\n";
    }
    else if (commandLine.options.censor)
    {
        std::cout << "No --model given: censoring only the manual --range entries.\n";
    }

    OfflinePipeline pipeline;
    const PipelineReport report = pipeline.run (audio, commandLine.options, recogniser.get());

    if (! writeWav (commandLine.outputPath, audio, commandLine.bitDepth, error))
    {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }

    std::cout << "---- Report ----\n";
    std::cout << "Loudness: " << report.inputLufs << " -> " << report.finalLufs << " LUFS\n";
    std::cout << "True peak: " << report.inputTruePeakDb << " -> " << report.outputTruePeakDb << " dBFS\n";
    std::cout << "Mastering gain: " << report.appliedGainDb << " dB, limiter reduction: "
              << report.limiterReductionDb << " dB\n";
    std::cout << "Non-speech ratio: " << report.nonSpeechRatio << "\n";
    std::cout << "Censored ranges: " << report.censoredRanges.size() << "\n";
    for (const auto& range : report.censoredRanges)
        std::cout << "  [" << range.start << ", " << range.end << ") samples\n";

    if (! report.transcript.empty())
    {
        ProfanityDetector detector;
        std::cout << "Transcript (" << report.transcript.size() << " words):\n";
        for (const auto& word : report.transcript)
            std::cout << (detector.isProfane (word.text) ? "  [*] " : "      ")
                      << "[" << word.startMs << "-" << word.endMs << " ms] " << word.text << "\n";
    }

    if (report.inputSamples != report.outputSamples)
    {
        std::cerr << "ERROR: sample count changed (" << report.inputSamples
                  << " -> " << report.outputSamples << ")\n";
        return 2;
    }
    std::cout << "Duration preserved: " << report.inputSamples << " samples.\n";
    return 0;
}
