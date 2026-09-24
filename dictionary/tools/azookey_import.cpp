#include "azookey_import.h"

#include "astelio/utf.h"

#include <cmath>
#include <cstring>

namespace astelio::azookey {
namespace {

template <typename T>
T Read(std::span<const std::byte> bytes, std::size_t offset)
{
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

bool ParseRecord(std::span<const std::byte> record, std::vector<Entry>& out)
{
    if (record.size() < 2) {
        return false;
    }
    const std::size_t count = Read<std::uint16_t>(record, 0);
    if (count == 0) {
        return true;
    }
    const std::size_t text_offset = 2 + count * 10;
    if (text_offset > record.size()) {
        return false;
    }
    const std::string_view text(reinterpret_cast<const char*>(record.data() + text_offset),
                                record.size() - text_offset);
    std::vector<std::string_view> fields;
    for (std::size_t start = 0;;) {
        const std::size_t tab = text.find('\t', start);
        fields.push_back(text.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos) {
            break;
        }
        start = tab + 1;
    }
    if (fields.size() != count + 1) {
        return false;
    }
    const std::optional<std::u16string> ruby = Utf8ToUtf16(fields[0]);
    if (!ruby || ruby->empty()) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = 2 + i * 10;
        Entry entry;
        entry.reading = *ruby;
        entry.left_id = Read<std::uint16_t>(record, at);
        entry.right_id = Read<std::uint16_t>(record, at + 2);
        entry.meaning_id = Read<std::uint16_t>(record, at + 4);
        entry.value = Read<float>(record, at + 6);
        if (fields[i + 1].empty()) {
            entry.surface = *ruby;
        } else {
            std::optional<std::u16string> word = Utf8ToUtf16(fields[i + 1]);
            if (!word) {
                return false;
            }
            entry.surface = std::move(*word);
        }
        out.push_back(std::move(entry));
    }
    return true;
}

} // namespace

bool ParseLoudsText(std::span<const std::byte> bytes, std::vector<Entry>& out)
{
    if (bytes.size() < 2) {
        return false;
    }
    const std::size_t count = Read<std::uint16_t>(bytes, 0);
    if (2 + count * 4 > bytes.size()) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t start = Read<std::uint32_t>(bytes, 2 + i * 4);
        const std::size_t end = i + 1 == count ? bytes.size() : Read<std::uint32_t>(bytes, 2 + (i + 1) * 4);
        if (start > end || end > bytes.size() || !ParseRecord(bytes.subspan(start, end - start), out)) {
            return false;
        }
    }
    return true;
}

std::optional<std::vector<std::pair<std::int32_t, float>>> ParseConnectionRow(std::span<const std::byte> bytes)
{
    if (bytes.size() < 8 || bytes.size() % 8 != 0) {
        return std::nullopt;
    }
    std::vector<std::pair<std::int32_t, float>> row;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 8) {
        row.emplace_back(Read<std::int32_t>(bytes, offset), Read<float>(bytes, offset + 4));
    }
    if (row.front().first != -1) {
        return std::nullopt;
    }
    return row;
}

std::u16string KatakanaToHiragana(std::u16string_view text)
{
    std::u16string result(text);
    for (char16_t& c : result) {
        if ((c >= u'\u30A1' && c <= u'\u30F6') || c == u'\u30FD' || c == u'\u30FE') {
            c = static_cast<char16_t>(c - 0x60);
        }
    }
    return result;
}

std::int16_t ToCost(float value)
{
    if (std::isnan(value)) {
        return INT16_MAX;
    }
    const double cost = std::round(-static_cast<double>(value) * 100.0);
    if (cost <= INT16_MIN) {
        return INT16_MIN;
    }
    if (cost >= INT16_MAX) {
        return INT16_MAX;
    }
    return static_cast<std::int16_t>(cost);
}

} // namespace astelio::azookey
