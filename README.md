# Podcast Cleaner

Podcast Cleaner is a macOS toolkit for cleaning up podcast recordings. It ships three front-ends that share one JUCE-free DSP/ML core:

- **`Podcast Cleaner.vst3`** - a JUCE 7 VST3 effect for streaming inside a host such as Audacity. It applies a classical in-place chain (high-pass, gate, compressor, EQ, sample-peak limiter) and never changes the sample count.
- **`podcast-cleaner`** - an offline command-line application that performs the full cleanup requested for this project: speech-aware noise reduction, **speech recognition and profanity censoring**, and **mastering preparation** (LUFS normalisation + true-peak limiting). Every stage preserves the exact duration of the input.
- **`Podcast Cleaner.app`** - a JUCE 7 desktop GUI over the same offline pipeline, aimed at users who do not want to remember flags: pick an input and an output file, decide whether to apply profanity censoring, dial in how much noise reduction you want, and press **Process**.

## Offline app (podcast-cleaner)

### 1. Cleanup of non-speech sounds and noise (duration preserving)
- Self-contained STFT (own radix-2 FFT, periodic Hann, weighted overlap-add, exact inverse). Interior frames are not re-zeroed, so the transform avoids redundant memory traffic.
- `VoiceActivityDetector`: model-free speech/non-speech detection from short-time SNR and spectral flatness. The per-bin noise floor uses **minimum statistics** (block-wise minimum with bias compensation, frozen while speech is present), so loud speech can never inflate the estimate. Each frame allocates nothing.
- `SpectralDenoiser`: a **decision-directed Wiener filter** (Ephraim-Malah style a priori SNR) instead of hard spectral subtraction. The Wiener gain tends to unity for speech-dominated bins, so speech, breaths and soft word endings are preserved by construction; a per-bin speech factor keeps noise-only bins suppressed inside speech frames, and 3-tap frequency plus one-pole temporal smoothing keep the residual noise natural. Output length always equals the input length. On a synthetic speech-in-noise test the noise drops to ~0.47x while speech is kept at ~0.99x.

### 2. Speech recognition and profanity censoring
- `WhisperTranscriptProvider` wraps whisper.cpp (fully offline, RU + EN, word-level timecodes). Recognition quality options:
  - **beam search** (default width 5) instead of greedy decoding,
  - optional **Silero VAD** pre-pass, which strips silence/music before the model runs (fewer hallucinations, and it skips audio so it is faster); word timecodes are mapped back to the original timeline, so censoring stays sample-accurate,
  - suppression of non-speech tokens and a temperature fallback for bad decodes.
- `ProfanityDetector`: bilingual dictionary with obfuscation-tolerant normalisation (case folding, Latin/Cyrillic look-alikes, repeated-letter collapsing). Russian roots are matched as substrings, English entries as whole words. The dictionary is decoded to code points once, so matching a word allocates nothing.
- `CensorStage`: sample-accurate mute or beep over each matched word, with click-free fades. Duration preserved.

### 3. Mastering preparation
- `LoudnessMeter`: ITU-R BS.1770 / EBU R128 K-weighted integrated loudness with absolute and relative gating.
- Normalisation to a target LUFS followed by a look-ahead `TruePeakLimiter` at a configurable ceiling.

## VST3 plugin (implemented)

- 80 Hz, second-order high-pass filter (12 dB/octave)
- Adjustable noise gate threshold
- Compressor with 3:1 default ratio, 10 ms attack, 100 ms release, and -18 dB threshold
- Podcast EQ: -3 dB at 300 Hz and +2 dB at 3 kHz
- Sample-peak limiter at -1 dBFS
- Output gain, stage enable switches, and saved plugin parameters
- Mono/stereo bus layouts; processing does not add or remove samples and works in host offline rendering

The plugin stays real-time/streaming, so the ASR-based censoring and two-pass LUFS mastering live in the offline `podcast-cleaner` app (they require an analysis pass over a whole file).

## GUI app (Podcast Cleaner.app)

A single window that exposes only what a normal user actually needs:

1. **Input file** / **Output file** - WAV paths, each with a `Choose...` button. The paths double as text fields, so they can be pasted. Choosing an input auto-fills a `<name>_clean.wav` output next to it.
2. **Apply profanity censoring** - the on/off switch for the ASR censoring. When it is off the app does denoising + mastering only and never loads a model. When it is on, the language picker and the Whisper model path appear beneath it (recognised automatically from `models/ggml-base.bin`; the Silero VAD model sitting next to it is picked up too). Censored words are muted, sample-accurately, without changing the duration.
3. **Noise reduction** - one slider, in dB, from `0 dB` (off) to `24 dB` (aggressive), default `12 dB`. It maps straight onto the spectral denoiser's maximum attenuation; speech protection stays at the tuned default.
4. **Process** - a single primary button. While it runs, the button is disabled, a progress bar appears and the other controls are locked, so a second job can never start.
5. **Report** - the same text report the CLI prints: LUFS before/after, true peak, mastering gain, limiter reduction, non-speech ratio, the censored sample ranges and the transcript with profane words marked `[*]`. A prominent line warns if the sample count ever changed.

The window can be resized and the report area grows with it. A status line under the button shows validation errors (missing input, output equal to input, no model chosen) before any work starts.

## Requirements

- macOS 12 or later
- CMake 3.22 or later
- Xcode command-line tools
- JUCE 7.0.12 and whisper.cpp, both attached as git submodules
- A ggml Whisper model under `models/` (see below)

Clone everything in one go with `git clone --recurse-submodules <repository-url>`, or run `git submodule update --init --recursive` in an existing checkout.

The setup used for this checkout keeps its CMake binary in `$HOME/Library/Developer/PodcastCleaner/BuildTools`; it does not change the system toolchain `PATH`. The default macOS architecture setting is a universal `arm64;x86_64` build; override it with `-DCMAKE_OSX_ARCHITECTURES=arm64` or `-DCMAKE_OSX_ARCHITECTURES=x86_64` when building for one architecture. The offline app builds for the host architecture only.

The local JUCE checkout includes a compatibility guard for SDK 15 and newer, where Apple's SDK marks the JUCE 7 window-snapshot API unavailable. That non-audio helper returns an empty image on newer SDKs; plugin audio processing is unaffected. VST2 replacement support is disabled because this project builds VST3 only. A fresh submodule checkout does not contain that guard, so it is also kept as a patch:

```sh
git -C JUCE apply ../packaging/patches/juce-7.0.12-macos15-screencapture.patch
```

### Dependencies

```sh
git submodule update --init --recursive
curl -L -o models/ggml-base.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin
curl -L -o models/ggml-silero-v6.2.0.bin \
  https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin
```

`ggml-base.bin` is multilingual (~148 MB) and handles Russian and English. Larger models (`ggml-small.bin`, `ggml-medium.bin`) improve recognition accuracy at the cost of speed and bundle size. The Silero VAD model is tiny (~0.9 MB); when it sits next to the Whisper model it is picked up automatically.

## Build

### VST3 plugin

From the `PodcastCleaner` directory:

```sh
CMAKE="$HOME/Library/Developer/PodcastCleaner/BuildTools/cmake-3.31.6-macos-universal/CMake.app/Contents/bin/cmake"
"$CMAKE" -S src -B build -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build build --config Release --target PodcastCleaner_VST3 --parallel
```

The VST3 bundle is produced under `build/PodcastCleaner_artefacts/Release/VST3`. Copy `Podcast Cleaner.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and rescan effects in Audacity.

### Offline app

```sh
"$CMAKE" -S src/app -B build-app -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build build-app --parallel
```

This produces `build-app/podcast-cleaner`. The first configure compiles whisper.cpp and ggml, so it takes a few minutes.

### GUI app

The same CMake project also builds the desktop GUI (`PODCASTCLEANER_BUILD_GUI` is `ON` by default), and both front-ends link the same `podcast-cleaner-lib` static library so they can never drift apart:

```sh
"$CMAKE" -S src/app -B build-app -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build build-app --target podcast-cleaner-gui --parallel
open "build-app/podcast-cleaner-gui_artefacts/Release/Podcast Cleaner.app"
```

Skip JUCE entirely with `-DPODCASTCLEANER_BUILD_GUI=OFF`. If the JUCE checkout is not at `PodcastCleaner/JUCE`, point at it with `-DJUCE_ROOT=/path/to/JUCE`.

### Installer (.dmg)

`packaging/make_dmg.sh` turns the GUI into a drag-and-drop disk image. It configures and builds `podcast-cleaner-gui`, stages the bundle next to an `/Applications` symlink and a short read-me, copies the Whisper and Silero models into `Podcast Cleaner.app/Contents/Resources/models`, renders the app icon (`packaging/make_icon.c` via `iconutil`), ad-hoc signs the bundle and verifies the finished image:

```sh
./packaging/make_dmg.sh                       # -> dist/PodcastCleaner-<version>.dmg
./packaging/make_dmg.sh -b build-app          # reuse an existing build tree
./packaging/make_dmg.sh --no-model            # model-free image
./packaging/make_dmg.sh -h                    # all options
```

The app bundle is self-contained, so the installed app needs no extra setup: the GUI looks for a model inside its own bundle first, then in Application Support, then next to the working directory or the executable. The script finds CMake even when it is not on `PATH` (the `CMAKE` variable, an existing build cache, `CMake.app`, or an IDE tool folder). Signing is ad-hoc, so the first launch needs a right-click and **Open**; distributing outside this machine requires an Apple developer identity.

## Using the offline app

```sh
./build-app/podcast-cleaner input.wav output.wav \
    --model models/ggml-base.bin --language ru \
    --censor --censor-mode mute --beam-size 5 \
    --denoise-reduction 12 --speech-protection 0.9 --vad-sensitivity 0.5 \
    --target-lufs -16 --ceiling-db -1 --bit-depth 16
```

Recognition tuning flags: `--beam-size <n>` (1 = greedy), `--vad-model <path>` and `--no-vad` (Silero VAD is auto-detected next to `--model`). Denoising flags: `--denoise-reduction` (max attenuation), `--speech-protection` (0..1, how aggressively speech is kept), `--vad-sensitivity`.

Run it with no arguments to print the full option list. It prints a report with input/output LUFS, true peak, mastering gain, limiter reduction, non-speech ratio, the censored ranges, and the full transcript with profane words marked `[*]`. The output always has exactly the same number of samples as the input.

## Processing behavior

Every stage is length-preserving, so the duration never changes. The VST3 processor handles blocks in place (identical sample count in and out) and is used by the host for real-time and offline rendering. The offline app works on a whole file: it denoises each channel, runs speech recognition once on a mono mixdown, censors the matched words, then normalises loudness and limits the true peak. Loudness measurement and gain application need an analysis pass over the whole file, which is exactly what the offline app and the GUI provide. The GUI thread only touches widgets; all reading, denoising, recognising, censoring and writing happen on a worker thread that publishes progress and the final report back to the message thread.
