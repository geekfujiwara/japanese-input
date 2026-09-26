#include "astelio/user_dictionary.h"

#include "astelio/utf.h"

#include <algorithm>

namespace astelio {
namespace {

constexpr std::string_view kHeader = "# astelio user dictionary 1";

bool SameWord(const UserDictionary::Word& a, const UserDictionary::Word& b)
{
    return a.pos == b.pos && a.reading == b.reading && a.surface == b.surface;
}

std::vector<std::string_view> Split(std::string_view line, char separator)
{
    std::vector<std::string_view> fields;
    for (std::size_t start = 0;;) {
        const std::size_t end = line.find(separator, start);
        fields.push_back(line.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) {
            return fields;
        }
        start = end + 1;
    }
}

std::optional<UserDictionary::Word> ParseLine(std::string_view line)
{
    const std::vector<std::string_view> fields = Split(line, '\t');
    if (fields.size() != 3 && fields.size() != 4) {
        return std::nullopt;
    }
    std::optional<std::u16string> reading = Utf8ToUtf16(fields[0]);
    std::optional<std::u16string> surface = Utf8ToUtf16(fields[1]);
    const std::optional<std::u16string> pos_name = Utf8ToUtf16(fields[2]);
    std::optional<std::u16string> comment =
        fields.size() == 4 ? Utf8ToUtf16(fields[3]) : std::optional<std::u16string>(std::u16string());
    if (!reading || !surface || !pos_name || !comment) {
        return std::nullopt;
    }
    const std::optional<UserDictionary::PartOfSpeech> pos = UserDictionary::PartOfSpeechFromName(*pos_name);
    if (!pos) {
        return std::nullopt;
    }
    return UserDictionary::Word{std::move(*reading), std::move(*surface), *pos, std::move(*comment)};
}

} // namespace

bool UserDictionary::Add(Word word)
{
    if (!Valid(word)) {
        return false;
    }
    const auto found = std::find_if(words_.begin(), words_.end(),
                                    [&word](const Word& stored) { return SameWord(stored, word); });
    if (found != words_.end()) {
        *found = std::move(word);
        return true;
    }
    if (words_.size() >= kMaxWords) {
        return false;
    }
    words_.push_back(std::move(word));
    return true;
}

bool UserDictionary::Update(const Word& old, Word word)
{
    if (!Valid(word)) {
        return false;
    }
    const auto found =
        std::find_if(words_.begin(), words_.end(), [&old](const Word& stored) { return SameWord(stored, old); });
    if (found == words_.end()) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(found - words_.begin());
    // The edited word may now be the same as another one; keep one of them.
    for (std::size_t i = 0; i < words_.size(); ++i) {
        if (i != index && SameWord(words_[i], word)) {
            words_.erase(words_.begin() + static_cast<std::ptrdiff_t>(i));
            words_[i < index ? index - 1 : index] = std::move(word);
            return true;
        }
    }
    words_[index] = std::move(word);
    return true;
}

bool UserDictionary::Remove(const Word& word)
{
    const auto found =
        std::find_if(words_.begin(), words_.end(), [&word](const Word& stored) { return SameWord(stored, word); });
    if (found == words_.end()) {
        return false;
    }
    words_.erase(found);
    return true;
}

void UserDictionary::Clear()
{
    words_.clear();
}

std::vector<UserDictionary::Word> UserDictionary::Search(std::u16string_view text) const
{
    std::vector<Word> found;
    for (const Word& word : words_) {
        if (word.reading.find(text) != std::u16string::npos || word.surface.find(text) != std::u16string::npos ||
            word.comment.find(text) != std::u16string::npos) {
            found.push_back(word);
        }
    }
    return found;
}

std::vector<std::u16string> UserDictionary::Lookup(std::u16string_view reading) const
{
    std::vector<std::u16string> surfaces;
    for (const Word& word : words_) {
        if (word.pos != PartOfSpeech::Suppressed && word.reading == reading &&
            std::find(surfaces.begin(), surfaces.end(), word.surface) == surfaces.end() &&
            !Suppressed(word.reading, word.surface)) {
            surfaces.push_back(word.surface);
        }
    }
    return surfaces;
}

std::vector<std::u16string> UserDictionary::Predict(std::u16string_view prefix, std::size_t limit) const
{
    std::vector<std::u16string> surfaces;
    for (const Word& word : words_) {
        if (surfaces.size() >= limit) {
            break;
        }
        if (word.pos != PartOfSpeech::Suppressed && word.reading.starts_with(prefix) &&
            std::find(surfaces.begin(), surfaces.end(), word.surface) == surfaces.end() &&
            !Suppressed(word.reading, word.surface)) {
            surfaces.push_back(word.surface);
        }
    }
    return surfaces;
}

bool UserDictionary::Suppressed(std::u16string_view reading, std::u16string_view surface) const
{
    return std::any_of(words_.begin(), words_.end(), [&](const Word& word) {
        return word.pos == PartOfSpeech::Suppressed && word.surface == surface &&
               (word.reading.empty() || reading.empty() || word.reading == reading);
    });
}

std::string UserDictionary::Serialize() const
{
    std::string text(kHeader);
    text += '\n';
    for (const Word& word : words_) {
        text += Utf16ToUtf8(word.reading);
        text += '\t';
        text += Utf16ToUtf8(word.surface);
        text += '\t';
        text += Utf16ToUtf8(PartOfSpeechName(word.pos));
        text += '\t';
        text += Utf16ToUtf8(word.comment);
        text += '\n';
    }
    return text;
}

UserDictionary UserDictionary::Parse(std::string_view text)
{
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3);
    }
    UserDictionary dictionary;
    for (std::string_view line : Split(text, '\n')) {
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        if (std::optional<Word> word = ParseLine(line)) {
            if (dictionary.words_.size() >= kMaxWords) {
                break;
            }
            dictionary.Add(std::move(*word));
        }
    }
    return dictionary;
}

} // namespace astelio
