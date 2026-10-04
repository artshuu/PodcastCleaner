#pragma once

#include "TranscriptProvider.h"

#include <string>
#include <vector>

namespace PodcastCleaner
{

/** Dictionary-based profanity detector.

    Operates on the word list produced by a TranscriptProvider. Matching is
    tolerant to typical obfuscation found in speech-to-text output:
      - case is folded and non-letter characters are stripped,
      - Latin look-alikes are folded to Cyrillic (x -> х, a -> а, ...),
      - runs of the same letter are collapsed (бляяя -> бля).

    Russian roots are matched as substrings (the language is heavily
    inflected); English entries are matched as whole words to avoid false
    positives such as "class" matching "ass". Dictionary entries are decoded to
    Unicode code points once, so matching allocates nothing per word. */
class ProfanityDetector
{
public:
    ProfanityDetector();

    /** Adds extra Russian roots (substring match). */
    void addRussianRoot (const std::string& root);

    /** Adds extra English words (whole-word match). */
    void addEnglishWord (const std::string& word);

    /** Returns true when the raw (un-normalised) word is profane. */
    bool isProfane (const std::string& rawWord) const;

    /** Returns the sample ranges of all profane words, padded by marginSamples
        on each side and clamped to [0, totalSamples). */
    std::vector<SampleRange> findRanges (const std::vector<TranscriptWord>& words,
                                         int sampleRate,
                                         std::int64_t totalSamples,
                                         std::int64_t marginSamples = 0) const;

    /** Normalises a word for dictionary matching (public for testing). */
    static std::string normalise (const std::string& rawWord);

private:
    std::vector<std::u32string> russianRoots;
    std::vector<std::u32string> englishWords;
};

} // namespace PodcastCleaner
