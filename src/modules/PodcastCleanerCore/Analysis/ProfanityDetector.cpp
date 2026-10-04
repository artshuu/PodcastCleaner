#include "ProfanityDetector.h"

#include <algorithm>
#include <cstdint>

namespace PodcastCleaner
{

namespace
{

std::u32string decodeUtf8 (const std::string& text)
{
    std::u32string out;
    size_t i = 0;
    while (i < text.size())
    {
        const unsigned char lead = static_cast<unsigned char> (text[i]);
        char32_t codePoint = 0;
        int extra = 0;
        if (lead < 0x80) { codePoint = lead; extra = 0; }
        else if ((lead >> 5) == 0x6) { codePoint = lead & 0x1F; extra = 1; }
        else if ((lead >> 4) == 0xE) { codePoint = lead & 0x0F; extra = 2; }
        else if ((lead >> 3) == 0x1E) { codePoint = lead & 0x07; extra = 3; }
        else { ++i; continue; }

        for (int k = 0; k < extra && i + 1 < text.size(); ++k)
        {
            ++i;
            codePoint = (codePoint << 6) | (static_cast<unsigned char> (text[i]) & 0x3F);
        }
        ++i;
        out.push_back (codePoint);
    }
    return out;
}

std::string encodeUtf8 (const std::u32string& text)
{
    std::string out;
    for (char32_t cp : text)
    {
        if (cp < 0x80)
            out.push_back (static_cast<char> (cp));
        else if (cp < 0x800)
        {
            out.push_back (static_cast<char> (0xC0 | (cp >> 6)));
            out.push_back (static_cast<char> (0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back (static_cast<char> (0xE0 | (cp >> 12)));
            out.push_back (static_cast<char> (0x80 | ((cp >> 6) & 0x3F)));
            out.push_back (static_cast<char> (0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

char32_t toLowerCodePoint (char32_t cp)
{
    if (cp >= U'A' && cp <= U'Z')
        return cp + 32;
    if (cp >= 0x0410 && cp <= 0x042F) // Cyrillic А..Я -> а..я
        return cp + 0x20;
    if (cp == 0x0401)                 // Ё -> ё
        return 0x0451;
    return cp;
}

char32_t foldLatinLookalike (char32_t cp)
{
    switch (cp)
    {
        case U'a': return 0x0430;
        case U'c': return 0x0441;
        case U'e': return 0x0435;
        case U'o': return 0x043E;
        case U'p': return 0x0440;
        case U'x': return 0x0445;
        case U'y': return 0x0443;
        case U'k': return 0x043A;
        case U'm': return 0x043C;
        case U't': return 0x0442;
        case U'h': return 0x043D;
        case U'b': return 0x0432;
        default:   return cp;
    }
}

bool isCyrillicOrLatin (char32_t cp)
{
    return (cp >= U'a' && cp <= U'z')
        || (cp >= 0x0430 && cp <= 0x044F) || cp == 0x0451;
}

std::u32string normaliseToCodePoints (const std::u32string& decoded)
{
    std::u32string cleaned;
    for (char32_t raw : decoded)
    {
        char32_t cp = foldLatinLookalike (toLowerCodePoint (raw));
        if (! isCyrillicOrLatin (cp))
            continue;
        if (! cleaned.empty() && cleaned.back() == cp)
            continue; // collapse runs of the same letter
        cleaned.push_back (cp);
    }
    return cleaned;
}

} // namespace

ProfanityDetector::ProfanityDetector()
{
    // Russian roots (substring match: the language is highly inflected).
    // Roots are kept at 3+ letters where a shorter form would over-match
    // (e.g. "еб" would flag "хлеб", "тебе", "себе").
    const char* roots[] = {
        "хуй", "хуя", "хую", "хуе", "хуё", "хуи", "хуйн",
        "пизд", "бля", "блят", "залуп",
        "мудак", "мудил", "мудач",
        "еба", "ебал", "ебан", "ебуч", "ебет", "еби",
        "ёба", "ёбал", "ёбан", "ёбуч", "ёбет", "ёби",
        "долбоеб", "долбоёб", "уебан", "уёбан", "еблан",
        "гандон", "гондон", "пидор", "пидар", "педик", "пидр",
        "сука", "суки", "сучк",
        "ахуе", "охуе", "нахуй", "похуй", "хуев", "хуёв",
        "манда", "шлюх", "мразь", "тварь", "говн", "дерьм",
        "жоп", "срак", "срат", "ссат", "дроч"
    };
    for (const char* root : roots)
        addRussianRoot (root);

    // English words (whole-word match to avoid "class"/"ass"-style hits).
    const char* words[] = {
        "fuck", "fucking", "fucked", "fucker", "shit", "shitty", "bitch",
        "bastard", "asshole", "dick", "pussy", "cunt", "faggot", "nigger",
        "nigga", "motherfucker", "cocksucker", "whore", "slut"
    };
    for (const char* word : words)
        addEnglishWord (word);
}

void ProfanityDetector::addRussianRoot (const std::string& root)
{
    const std::u32string decoded = normaliseToCodePoints (decodeUtf8 (root));
    if (! decoded.empty())
        russianRoots.push_back (decoded);
}

void ProfanityDetector::addEnglishWord (const std::string& word)
{
    const std::u32string decoded = normaliseToCodePoints (decodeUtf8 (word));
    if (! decoded.empty())
        englishWords.push_back (decoded);
}

std::string ProfanityDetector::normalise (const std::string& rawWord)
{
    return encodeUtf8 (normaliseToCodePoints (decodeUtf8 (rawWord)));
}

bool ProfanityDetector::isProfane (const std::string& rawWord) const
{
    const std::u32string word = normaliseToCodePoints (decodeUtf8 (rawWord));
    if (word.empty())
        return false;

    for (const auto& root : russianRoots)
        if (word.find (root) != std::u32string::npos)
            return true;

    for (const auto& entry : englishWords)
        if (entry == word)
            return true;

    return false;
}

std::vector<SampleRange> ProfanityDetector::findRanges (const std::vector<TranscriptWord>& words,
                                                        int sampleRate,
                                                        std::int64_t totalSamples,
                                                        std::int64_t marginSamples) const
{
    std::vector<SampleRange> ranges;
    if (sampleRate <= 0)
        return ranges;

    for (const auto& word : words)
    {
        if (! isProfane (word.text))
            continue;

        SampleRange range;
        range.start = (word.startMs * sampleRate) / 1000 - marginSamples;
        range.end = (word.endMs * sampleRate) / 1000 + marginSamples;
        range.start = std::max<std::int64_t> (0, range.start);
        range.end = std::min<std::int64_t> (totalSamples, range.end);
        if (range.end > range.start)
            ranges.push_back (range);
    }

    std::sort (ranges.begin(), ranges.end(),
               [] (const SampleRange& a, const SampleRange& b) { return a.start < b.start; });

    std::vector<SampleRange> merged;
    for (const auto& range : ranges)
    {
        if (! merged.empty() && range.start <= merged.back().end)
            merged.back().end = std::max (merged.back().end, range.end);
        else
            merged.push_back (range);
    }
    return merged;
}

} // namespace PodcastCleaner
