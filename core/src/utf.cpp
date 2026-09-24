#include "astelio/utf.h"

#include <cstddef>

namespace astelio {
namespace {

void AppendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool IsHighSurrogate(char32_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLowSurrogate(char32_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

} // namespace

std::optional<std::u16string> Utf8ToUtf16(std::string_view utf8)
{
    std::u16string out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        char32_t cp = 0;
        std::size_t length = 0;
        if (lead < 0x80) {
            cp = lead;
            length = 1;
        } else if ((lead >> 5) == 0x6) {
            cp = lead & 0x1Fu;
            length = 2;
        } else if ((lead >> 4) == 0xE) {
            cp = lead & 0x0Fu;
            length = 3;
        } else if ((lead >> 3) == 0x1E) {
            cp = lead & 0x07u;
            length = 4;
        } else {
            return std::nullopt;
        }
        if (utf8.size() - i < length) {
            return std::nullopt;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(utf8[i + k]);
            if ((next >> 6) != 0x2) {
                return std::nullopt;
            }
            cp = (cp << 6) | (next & 0x3Fu);
        }
        const bool overlong = (length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) || (length == 4 && cp < 0x10000);
        if (overlong || cp > 0x10FFFF || IsHighSurrogate(cp) || IsLowSurrogate(cp)) {
            return std::nullopt;
        }
        if (cp < 0x10000) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
        i += length;
    }
    return out;
}

std::string Utf16ToUtf8(std::u16string_view utf16)
{
    std::string out;
    out.reserve(utf16.size() * 3);
    for (std::size_t i = 0; i < utf16.size(); ++i) {
        const char32_t unit = utf16[i];
        if (IsHighSurrogate(unit) && i + 1 < utf16.size() && IsLowSurrogate(utf16[i + 1])) {
            const char32_t low = utf16[i + 1];
            AppendUtf8(out, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
            ++i;
        } else if (IsHighSurrogate(unit) || IsLowSurrogate(unit)) {
            AppendUtf8(out, 0xFFFD);
        } else {
            AppendUtf8(out, unit);
        }
    }
    return out;
}

} // namespace astelio
