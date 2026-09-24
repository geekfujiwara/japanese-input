// F-01: libFuzzer entry for the composer and the romaji table parser.
#include "astelio/composer.h"
#include "astelio/romaji_table_io.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

void Check(bool condition)
{
    if (!condition) {
        __builtin_trap();
    }
}

void DriveComposer(astelio::Composer& composer, const std::uint8_t* data, std::size_t size)
{
    for (std::size_t i = 0; i < size && i < 10000; ++i) {
        const std::uint8_t byte = data[i];
        if (byte < 0x80) {
            composer.InsertKey(static_cast<char16_t>(byte));
        } else {
            switch (byte & 0x07) {
            case 0: composer.Backspace(); break;
            case 1: composer.Delete(); break;
            case 2: composer.MoveLeft(); break;
            case 3: composer.MoveRight(); break;
            case 4: composer.ExitTemporaryAlphanumeric(); break;
            case 5: composer.Commit(); break;
            default: composer.Clear(); break;
            }
        }
        Check(composer.Cursor() <= composer.Text().size());
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    astelio::Composer composer(astelio::RomajiTable::Default(), astelio::CharacterSettings{});
    DriveComposer(composer, data, size);

    const std::string_view text(reinterpret_cast<const char*>(data), size);
    const astelio::RomajiTableParseResult parsed = astelio::ParseRomajiTable(text);
    if (parsed.table) {
        const astelio::RomajiTableParseResult again =
            astelio::ParseRomajiTable(astelio::SerializeRomajiTable(*parsed.table));
        Check(again.table.has_value());
        Check(again.table->rules().size() == parsed.table->rules().size());

        astelio::Composer custom(*parsed.table, astelio::CharacterSettings{});
        DriveComposer(custom, data, size);
    }
    return 0;
}
