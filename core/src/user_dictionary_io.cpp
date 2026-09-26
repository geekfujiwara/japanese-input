#include "astelio/user_dictionary_io.h"

#include "astelio/utf.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace astelio {
namespace {

using Pos = UserDictionary::PartOfSpeech;
using Word = UserDictionary::Word;

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
constexpr std::string_view kUtf16LeBom = "\xFF\xFE";
constexpr std::string_view kUtf16BeBom = "\xFE\xFF";

bool IsHighSurrogate(char16_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLowSurrogate(char16_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

std::optional<std::u16string> DecodeUtf16(std::string_view bytes, bool little_endian)
{
    if (bytes.size() % 2 != 0) {
        return std::nullopt;
    }
    std::u16string out;
    out.reserve(bytes.size() / 2);
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        const unsigned first = static_cast<unsigned char>(bytes[i]);
        const unsigned second = static_cast<unsigned char>(bytes[i + 1]);
        out.push_back(static_cast<char16_t>(little_endian ? (second << 8 | first) : (first << 8 | second)));
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (IsHighSurrogate(out[i]) && i + 1 < out.size() && IsLowSurrogate(out[i + 1])) {
            ++i;
        } else if (IsHighSurrogate(out[i]) || IsLowSurrogate(out[i])) {
            return std::nullopt;
        }
    }
    return out;
}

std::string EncodeUtf16(std::u16string_view text, bool little_endian)
{
    std::string out(little_endian ? kUtf16LeBom : kUtf16BeBom);
    out.reserve(2 + text.size() * 2);
    for (std::size_t i = 0; i < text.size(); ++i) {
        char16_t unit = text[i];
        const bool paired = (IsHighSurrogate(unit) && i + 1 < text.size() && IsLowSurrogate(text[i + 1])) ||
                            (IsLowSurrogate(unit) && i > 0 && IsHighSurrogate(text[i - 1]));
        if (!paired && (IsHighSurrogate(unit) || IsLowSurrogate(unit))) {
            unit = u'\uFFFD';
        }
        const char high = static_cast<char>(unit >> 8);
        const char low = static_cast<char>(unit & 0xFF);
        out.push_back(little_endian ? low : high);
        out.push_back(little_endian ? high : low);
    }
    return out;
}

// --- Parts of speech (the table under T-D03-1 in the test plan) ---

struct PosName {
    std::u16string_view name;
    Pos pos;
};

// Names of the other IMEs; any of them may appear in any text format.
constexpr PosName kImportNames[] = {
    // Google Japanese Input (Mozc)
    {u"名詞", Pos::Noun},
    {u"固有名詞", Pos::ProperNoun},
    {u"人名", Pos::PersonName},
    {u"姓", Pos::PersonName},
    {u"名", Pos::PersonName},
    {u"地名", Pos::PlaceName},
    {u"組織", Pos::Organization},
    {u"短縮よみ", Pos::Abbreviation},
    {u"記号", Pos::Symbol},
    {u"顔文字", Pos::Symbol},
    {u"句読点", Pos::Symbol},
    {u"抑制単語", Pos::Suppressed},
    {u"名詞サ変", Pos::Noun},
    {u"名詞形動", Pos::Noun},
    {u"数", Pos::Noun},
    {u"アルファベット", Pos::Noun},
    {u"副詞", Pos::Noun},
    {u"連体詞", Pos::Noun},
    {u"接続詞", Pos::Noun},
    {u"感動詞", Pos::Noun},
    {u"接頭語", Pos::Noun},
    {u"助数詞", Pos::Noun},
    {u"接尾一般", Pos::Noun},
    {u"接尾人名", Pos::Noun},
    {u"接尾地名", Pos::Noun},
    {u"終助詞", Pos::Noun},
    {u"独立語", Pos::Noun},
    {u"品詞なし", Pos::Noun},
    {u"サジェストのみ", Pos::Noun},
    // MS-IME
    {u"地名その他", Pos::PlaceName},
    {u"さ変名詞", Pos::Noun},
    {u"ざ変名詞", Pos::Noun},
    {u"形動名詞", Pos::Noun},
    {u"さ変形動名詞", Pos::Noun},
    {u"副詞的名詞", Pos::Noun},
    {u"接尾語", Pos::Noun},
    {u"姓名接頭語", Pos::Noun},
    {u"地名接頭語", Pos::Noun},
    {u"姓名接尾語", Pos::Noun},
    {u"地名接尾語", Pos::Noun},
    {u"その他自立語", Pos::Noun},
    {u"慣用句", Pos::Noun},
    {u"単漢字", Pos::Noun},
    // ATOK
    {u"固有一般", Pos::ProperNoun},
    {u"固有商品", Pos::ProperNoun},
    {u"固有人姓", Pos::PersonName},
    {u"固有人名", Pos::PersonName},
    {u"固有人他", Pos::PersonName},
    {u"固有地名", Pos::PlaceName},
    {u"固有組織", Pos::Organization},
    {u"短縮読み", Pos::Abbreviation},
    {u"名詞ザ変", Pos::Noun},
    {u"名サ形動", Pos::Noun},
    {u"数詞", Pos::Noun},
    {u"冠数詞", Pos::Noun},
    // Kotoeri
    {u"普通名詞", Pos::Noun},
    {u"その他の固有名詞", Pos::ProperNoun},
    {u"組織名", Pos::Organization},
    {u"サ変名詞", Pos::Noun},
    {u"人名接尾語", Pos::Noun},
    {u"組織名接尾語", Pos::Noun},
    {u"数字列接尾語", Pos::Noun},
    {u"無品詞", Pos::Noun},
};

// Verbs and adjectives of every IME ("動詞ラ行五段", "ら行五段", "形容詞ｶﾞﾙ", "ラ変動詞", ...) become nouns.
bool IsInflecting(std::u16string_view name)
{
    for (const std::u16string_view part : {u"動詞", u"五段", u"四段", u"一段", u"二段", u"変格"}) {
        if (name.find(part) != std::u16string_view::npos) {
            return true;
        }
    }
    return name.starts_with(u"形容") || name.starts_with(u"形動");
}

std::optional<Pos> PosFromImportName(std::u16string_view name)
{
    // ATOK marks words it registered itself with '*' or '$'.
    while (!name.empty() && (name.back() == u'*' || name.back() == u'$')) {
        name.remove_suffix(1);
    }
    for (const auto& entry : kImportNames) {
        if (entry.name == name) {
            return entry.pos;
        }
    }
    if (!name.empty() && IsInflecting(name)) {
        return Pos::Noun;
    }
    return std::nullopt;
}

// Index: Noun, ProperNoun, PersonName, PlaceName, Organization, Abbreviation, Symbol, Suppressed.
// An empty name: the format has no such word, and it is not written.
constexpr std::u16string_view kMsImeNames[] = {
    u"名詞", u"固有名詞", u"人名", u"地名その他", u"固有名詞", u"短縮よみ", u"顔文字", u"抑制単語",
};
constexpr std::u16string_view kAtokNames[] = {
    u"名詞", u"固有一般", u"固有人他", u"固有地名", u"固有組織", u"短縮読み", u"顔文字", u"",
};
constexpr std::u16string_view kKotoeriNames[] = {
    u"普通名詞", u"固有名詞", u"人名", u"地名", u"固有名詞", u"普通名詞", u"普通名詞", u"",
};

std::u16string_view ExportName(Pos pos, UserDictionaryFormat format)
{
    const auto index = static_cast<std::size_t>(pos);
    switch (format) {
    case UserDictionaryFormat::MsIme:
        return kMsImeNames[index];
    case UserDictionaryFormat::Atok:
        return kAtokNames[index];
    case UserDictionaryFormat::Kotoeri:
        return kKotoeriNames[index];
    case UserDictionaryFormat::GoogleIme:
    case UserDictionaryFormat::AstelioJson:
        break;
    }
    return UserDictionary::PartOfSpeechName(pos);
}

// --- Text formats ---

std::vector<std::u16string_view> SplitLines(std::u16string_view text)
{
    std::vector<std::u16string_view> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'\r' || text[i] == u'\n') {
            lines.push_back(text.substr(start, i - start));
            if (text[i] == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') {
                ++i;
            }
            start = i + 1;
        }
    }
    if (start < text.size()) {
        lines.push_back(text.substr(start));
    }
    return lines;
}

bool StartsWithIgnoringCase(std::u16string_view text, std::u16string_view lower_prefix)
{
    if (text.size() < lower_prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lower_prefix.size(); ++i) {
        char16_t c = text[i];
        if (c >= u'A' && c <= u'Z') {
            c = static_cast<char16_t>(c - u'A' + u'a');
        }
        if (c != lower_prefix[i]) {
            return false;
        }
    }
    return true;
}

bool IsComment(std::u16string_view line, UserDictionaryFormat format)
{
    switch (format) {
    case UserDictionaryFormat::GoogleIme:
        return line.starts_with(u'#');
    case UserDictionaryFormat::MsIme:
    case UserDictionaryFormat::Atok:
        return line.starts_with(u'!');
    case UserDictionaryFormat::Kotoeri:
        return line.starts_with(u"//");
    case UserDictionaryFormat::AstelioJson:
        break;
    }
    return false;
}

std::optional<UserDictionaryFormat> DetectTextFormat(const std::vector<std::u16string_view>& lines)
{
    for (const auto line : lines) {
        if (line.empty() || line.starts_with(u"//")) {
            continue;
        }
        if (StartsWithIgnoringCase(line, u"!microsoft ime")) {
            return UserDictionaryFormat::MsIme;
        }
        if (StartsWithIgnoringCase(line, u"!!atok_tango_text_header") || StartsWithIgnoringCase(line, u"!!dicut")) {
            return UserDictionaryFormat::Atok;
        }
        const bool has_tab = line.find(u'\t') != std::u16string_view::npos;
        if (line.size() >= 2 && line.front() == u'"' && line.back() == u'"' && !has_tab) {
            return UserDictionaryFormat::Kotoeri;
        }
        if (line.front() == u'#' || has_tab) {
            return UserDictionaryFormat::GoogleIme;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<std::u16string_view> SplitTabs(std::u16string_view line)
{
    std::vector<std::u16string_view> fields;
    std::size_t start = 0;
    while (true) {
        const auto tab = line.find(u'\t', start);
        if (tab == std::u16string_view::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

// Kotoeri: "reading","surface","part of speech" with "" for a quote inside a field. False when malformed.
bool SplitCsv(std::u16string_view line, std::vector<std::u16string>& fields)
{
    std::size_t i = 0;
    while (true) {
        std::u16string field;
        if (i < line.size() && line[i] == u'"') {
            ++i;
            while (true) {
                if (i >= line.size()) {
                    return false;
                }
                if (line[i] == u'"') {
                    if (i + 1 < line.size() && line[i + 1] == u'"') {
                        field.push_back(u'"');
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                field.push_back(line[i++]);
            }
        } else {
            while (i < line.size() && line[i] != u',') {
                field.push_back(line[i++]);
            }
        }
        fields.push_back(std::move(field));
        if (i == line.size()) {
            return true;
        }
        if (line[i] != u',') {
            return false;
        }
        ++i;
    }
}

std::u16string NormalizeReading(std::u16string_view reading)
{
    std::u16string out(reading);
    for (auto& c : out) {
        if (c >= u'\u30A1' && c <= u'\u30F6') {
            c = static_cast<char16_t>(c - 0x60);
        }
    }
    return out;
}

std::optional<Word> MakeWord(std::u16string_view reading, std::u16string_view surface, std::u16string_view pos_name,
                             std::u16string_view comment)
{
    const auto pos = PosFromImportName(pos_name);
    if (!pos) {
        return std::nullopt;
    }
    Word word{NormalizeReading(reading), std::u16string(surface), *pos, std::u16string(comment)};
    if (!UserDictionary::Valid(word)) {
        return std::nullopt;
    }
    return word;
}

UserDictionaryImport ImportText(const std::vector<std::u16string_view>& lines, UserDictionaryFormat format)
{
    UserDictionaryImport result;
    result.format = format;
    std::vector<std::u16string> csv;
    for (const auto line : lines) {
        if (line.empty() || IsComment(line, format)) {
            continue;
        }
        std::optional<Word> word;
        if (format == UserDictionaryFormat::Kotoeri) {
            csv.clear();
            if (SplitCsv(line, csv) && csv.size() >= 3) {
                word = MakeWord(csv[0], csv[1], csv[2], u"");
            }
        } else {
            const auto fields = SplitTabs(line);
            if (fields.size() >= 3) {
                word = MakeWord(fields[0], fields[1], fields[2], fields.size() >= 4 ? fields[3] : u"");
            }
        }
        if (word) {
            result.words.push_back(std::move(*word));
        } else {
            ++result.skipped;
        }
    }
    return result;
}

// --- JSON ---

struct JsonValue {
    enum class Kind : std::uint8_t { Null, Boolean, Number, String, Array, Object };
    Kind kind;
    std::u16string text;              // a String, or a Number as written
    std::vector<JsonValue> items;     // Array items, or Object values
    std::vector<std::u16string> keys; // Object keys, one per value
};

// Only what our file needs; rejects anything that is not well formed JSON (numbers are checked loosely).
class JsonParser {
public:
    explicit JsonParser(std::u16string_view text) : text_(text) {}

    bool ParseDocument(JsonValue& value)
    {
        SkipSpace();
        if (!Parse(value, 0)) {
            return false;
        }
        SkipSpace();
        return pos_ == text_.size();
    }

private:
    static constexpr int kMaxDepth = 16;

    bool Parse(JsonValue& value, int depth)
    {
        if (depth > kMaxDepth || pos_ >= text_.size()) {
            return false;
        }
        switch (text_[pos_]) {
        case u'{':
            return ParseObject(value, depth);
        case u'[':
            return ParseArray(value, depth);
        case u'"':
            value.kind = JsonValue::Kind::String;
            return ParseString(value.text);
        case u't':
            value.kind = JsonValue::Kind::Boolean;
            return Consume(u"true");
        case u'f':
            value.kind = JsonValue::Kind::Boolean;
            return Consume(u"false");
        case u'n':
            value.kind = JsonValue::Kind::Null;
            return Consume(u"null");
        default:
            value.kind = JsonValue::Kind::Number;
            return ParseNumber(value.text);
        }
    }

    bool ParseObject(JsonValue& value, int depth)
    {
        value.kind = JsonValue::Kind::Object;
        ++pos_;
        SkipSpace();
        if (Next(u'}')) {
            return true;
        }
        while (true) {
            SkipSpace();
            std::u16string key;
            if (pos_ >= text_.size() || text_[pos_] != u'"' || !ParseString(key)) {
                return false;
            }
            SkipSpace();
            if (!Next(u':')) {
                return false;
            }
            SkipSpace();
            JsonValue member{};
            if (!Parse(member, depth + 1)) {
                return false;
            }
            value.keys.push_back(std::move(key));
            value.items.push_back(std::move(member));
            SkipSpace();
            if (Next(u'}')) {
                return true;
            }
            if (!Next(u',')) {
                return false;
            }
        }
    }

    bool ParseArray(JsonValue& value, int depth)
    {
        value.kind = JsonValue::Kind::Array;
        ++pos_;
        SkipSpace();
        if (Next(u']')) {
            return true;
        }
        while (true) {
            SkipSpace();
            JsonValue item{};
            if (!Parse(item, depth + 1)) {
                return false;
            }
            value.items.push_back(std::move(item));
            SkipSpace();
            if (Next(u']')) {
                return true;
            }
            if (!Next(u',')) {
                return false;
            }
        }
    }

    bool ParseString(std::u16string& out)
    {
        ++pos_;
        while (pos_ < text_.size()) {
            const char16_t c = text_[pos_++];
            if (c == u'"') {
                return true;
            }
            if (c < 0x20) {
                return false;
            }
            if (c != u'\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) {
                return false;
            }
            switch (const char16_t escape = text_[pos_++]) {
            case u'"':
            case u'\\':
            case u'/':
                out.push_back(escape);
                break;
            case u'b':
                out.push_back(u'\b');
                break;
            case u'f':
                out.push_back(u'\f');
                break;
            case u'n':
                out.push_back(u'\n');
                break;
            case u'r':
                out.push_back(u'\r');
                break;
            case u't':
                out.push_back(u'\t');
                break;
            case u'u': {
                if (text_.size() - pos_ < 4) {
                    return false;
                }
                unsigned code = 0;
                for (int k = 0; k < 4; ++k) {
                    const char16_t h = text_[pos_++];
                    unsigned digit = 0;
                    if (h >= u'0' && h <= u'9') {
                        digit = static_cast<unsigned>(h - u'0');
                    } else if (h >= u'a' && h <= u'f') {
                        digit = static_cast<unsigned>(h - u'a' + 10);
                    } else if (h >= u'A' && h <= u'F') {
                        digit = static_cast<unsigned>(h - u'A' + 10);
                    } else {
                        return false;
                    }
                    code = code << 4 | digit;
                }
                out.push_back(static_cast<char16_t>(code));
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool ParseNumber(std::u16string& out)
    {
        const std::size_t start = pos_;
        while (pos_ < text_.size() && ((text_[pos_] >= u'0' && text_[pos_] <= u'9') || text_[pos_] == u'-' ||
                                       text_[pos_] == u'+' || text_[pos_] == u'.' || text_[pos_] == u'e' ||
                                       text_[pos_] == u'E')) {
            ++pos_;
        }
        out.assign(text_.substr(start, pos_ - start));
        return pos_ > start;
    }

    bool Consume(std::u16string_view word)
    {
        if (!text_.substr(pos_).starts_with(word)) {
            return false;
        }
        pos_ += word.size();
        return true;
    }

    bool Next(char16_t c)
    {
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    void SkipSpace()
    {
        while (pos_ < text_.size() &&
               (text_[pos_] == u' ' || text_[pos_] == u'\t' || text_[pos_] == u'\r' || text_[pos_] == u'\n')) {
            ++pos_;
        }
    }

    std::u16string_view text_;
    std::size_t pos_ = 0;
};

const JsonValue* Member(const JsonValue& object, std::u16string_view key)
{
    for (std::size_t i = 0; i < object.keys.size(); ++i) {
        if (object.keys[i] == key) {
            return &object.items[i];
        }
    }
    return nullptr;
}

// A string member, or `fallback` when it is missing; nullopt when it is not a string.
std::optional<std::u16string> StringMember(const JsonValue& object, std::u16string_view key,
                                           std::optional<std::u16string> fallback)
{
    const JsonValue* value = Member(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (value->kind != JsonValue::Kind::String) {
        return std::nullopt;
    }
    return value->text;
}

std::optional<Word> JsonWord(const JsonValue& item)
{
    if (item.kind != JsonValue::Kind::Object) {
        return std::nullopt;
    }
    const auto reading = StringMember(item, u"reading", u"");
    const auto surface = StringMember(item, u"surface", std::nullopt);
    const auto pos_name = StringMember(item, u"pos", std::u16string(UserDictionary::PartOfSpeechName(Pos::Noun)));
    const auto comment = StringMember(item, u"comment", u"");
    if (!reading || !surface || !pos_name || !comment) {
        return std::nullopt;
    }
    const auto pos = UserDictionary::PartOfSpeechFromName(*pos_name);
    if (!pos) {
        return std::nullopt;
    }
    Word word{*reading, *surface, *pos, *comment};
    if (!UserDictionary::Valid(word)) {
        return std::nullopt;
    }
    return word;
}

UserDictionaryImport ImportJson(std::u16string_view text)
{
    UserDictionaryImport result;
    JsonValue root{};
    if (!JsonParser(text).ParseDocument(root) || root.kind != JsonValue::Kind::Object) {
        return result;
    }
    const JsonValue* version = Member(root, u"version");
    const JsonValue* words = Member(root, u"words");
    if (version == nullptr || version->kind != JsonValue::Kind::Number || version->text != u"1" ||
        words == nullptr || words->kind != JsonValue::Kind::Array) {
        return result;
    }
    result.format = UserDictionaryFormat::AstelioJson;
    for (const auto& item : words->items) {
        if (auto word = JsonWord(item)) {
            result.words.push_back(std::move(*word));
        } else {
            ++result.skipped;
        }
    }
    return result;
}

void AppendJsonString(std::u16string& out, std::u16string_view text)
{
    constexpr char16_t kHex[] = u"0123456789abcdef";
    out.push_back(u'"');
    for (const char16_t c : text) {
        switch (c) {
        case u'"':
            out += u"\\\"";
            break;
        case u'\\':
            out += u"\\\\";
            break;
        case u'\n':
            out += u"\\n";
            break;
        case u'\r':
            out += u"\\r";
            break;
        case u'\t':
            out += u"\\t";
            break;
        default:
            if (c < 0x20) {
                out += u"\\u00";
                out.push_back(kHex[c >> 4]);
                out.push_back(kHex[c & 0xF]);
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back(u'"');
}

std::u16string ExportJson(const std::vector<Word>& words)
{
    std::u16string out = u"{\n  \"version\": 1,\n  \"words\": [";
    bool first = true;
    for (const auto& word : words) {
        if (!UserDictionary::Valid(word)) {
            continue;
        }
        out += first ? u"\n    {\"reading\": " : u",\n    {\"reading\": ";
        first = false;
        AppendJsonString(out, word.reading);
        out += u", \"surface\": ";
        AppendJsonString(out, word.surface);
        out += u", \"pos\": ";
        AppendJsonString(out, UserDictionary::PartOfSpeechName(word.pos));
        out += u", \"comment\": ";
        AppendJsonString(out, word.comment);
        out += u"}";
    }
    out += first ? u"]\n}\n" : u"\n  ]\n}\n";
    return out;
}

void AppendCsvField(std::u16string& out, std::u16string_view text)
{
    out.push_back(u'"');
    for (const char16_t c : text) {
        if (c == u'"') {
            out.push_back(u'"');
        }
        out.push_back(c);
    }
    out.push_back(u'"');
}

} // namespace

TextEncoding DetectEncoding(std::string_view bytes)
{
    if (bytes.starts_with(kUtf8Bom)) {
        return TextEncoding::Utf8;
    }
    if (bytes.starts_with(kUtf16LeBom)) {
        return TextEncoding::Utf16Le;
    }
    if (bytes.starts_with(kUtf16BeBom)) {
        return TextEncoding::Utf16Be;
    }
    // UTF-8 and Shift_JIS text has no zero bytes; in UTF-16 the ASCII tabs and line breaks bring them.
    std::size_t even_zeros = 0;
    std::size_t odd_zeros = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '\0') {
            ++(i % 2 == 0 ? even_zeros : odd_zeros);
        }
    }
    if (even_zeros + odd_zeros > 0) {
        return odd_zeros >= even_zeros ? TextEncoding::Utf16Le : TextEncoding::Utf16Be;
    }
    return Utf8ToUtf16(bytes) ? TextEncoding::Utf8 : TextEncoding::ShiftJis;
}

std::optional<std::u16string> DecodeText(std::string_view bytes)
{
    switch (DetectEncoding(bytes)) {
    case TextEncoding::Utf8:
        if (bytes.starts_with(kUtf8Bom)) {
            bytes.remove_prefix(kUtf8Bom.size());
        }
        return Utf8ToUtf16(bytes);
    case TextEncoding::Utf16Le:
        if (bytes.starts_with(kUtf16LeBom)) {
            bytes.remove_prefix(kUtf16LeBom.size());
        }
        return DecodeUtf16(bytes, true);
    case TextEncoding::Utf16Be:
        if (bytes.starts_with(kUtf16BeBom)) {
            bytes.remove_prefix(kUtf16BeBom.size());
        }
        return DecodeUtf16(bytes, false);
    case TextEncoding::ShiftJis:
        break;
    }
    return std::nullopt;
}

std::optional<std::string> EncodeText(std::u16string_view text, TextEncoding encoding)
{
    switch (encoding) {
    case TextEncoding::Utf8:
        return Utf16ToUtf8(text);
    case TextEncoding::Utf16Le:
        return EncodeUtf16(text, true);
    case TextEncoding::Utf16Be:
        return EncodeUtf16(text, false);
    case TextEncoding::ShiftJis:
        break;
    }
    return std::nullopt;
}

UserDictionaryImport ImportUserDictionary(std::u16string_view text)
{
    if (text.starts_with(u'\uFEFF')) {
        text.remove_prefix(1);
    }
    const auto first = text.find_first_not_of(u" \t\r\n");
    if (first != std::u16string_view::npos && text[first] == u'{') {
        return ImportJson(text);
    }
    const auto lines = SplitLines(text);
    if (const auto format = DetectTextFormat(lines)) {
        return ImportText(lines, *format);
    }
    return {};
}

std::u16string ExportUserDictionary(const std::vector<UserDictionary::Word>& words, UserDictionaryFormat format)
{
    if (format == UserDictionaryFormat::AstelioJson) {
        return ExportJson(words);
    }
    std::u16string out;
    std::u16string_view line_end = u"\n";
    if (format == UserDictionaryFormat::MsIme) {
        out = u"!Microsoft IME Dictionary Tool\r\n!Format:WORDLIST\r\n\r\n";
        line_end = u"\r\n";
    } else if (format == UserDictionaryFormat::Atok) {
        out = u"!!ATOK_TANGO_TEXT_HEADER_1\r\n";
        line_end = u"\r\n";
    }
    for (const auto& word : words) {
        const auto pos_name = ExportName(word.pos, format);
        if (pos_name.empty() || !UserDictionary::Valid(word)) {
            continue;
        }
        if (format == UserDictionaryFormat::Kotoeri) {
            AppendCsvField(out, word.reading);
            out.push_back(u',');
            AppendCsvField(out, word.surface);
            out.push_back(u',');
            AppendCsvField(out, pos_name);
        } else {
            out.append(word.reading).append(u"\t").append(word.surface).append(u"\t").append(pos_name);
            if (!word.comment.empty() && format != UserDictionaryFormat::Atok) {
                out.append(u"\t").append(word.comment);
            }
        }
        out += line_end;
    }
    return out;
}

TextEncoding ExportEncoding(UserDictionaryFormat format)
{
    switch (format) {
    case UserDictionaryFormat::MsIme:
    case UserDictionaryFormat::Atok:
        return TextEncoding::Utf16Le;
    case UserDictionaryFormat::GoogleIme:
    case UserDictionaryFormat::Kotoeri:
    case UserDictionaryFormat::AstelioJson:
        break;
    }
    return TextEncoding::Utf8;
}

} // namespace astelio
