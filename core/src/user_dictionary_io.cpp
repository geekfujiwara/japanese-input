#include "astelio/user_dictionary_io.h"

namespace astelio {

TextEncoding DetectEncoding(std::string_view)
{
    return TextEncoding::Utf8;
}

std::optional<std::u16string> DecodeText(std::string_view)
{
    return std::nullopt;
}

std::optional<std::string> EncodeText(std::u16string_view, TextEncoding)
{
    return std::nullopt;
}

UserDictionaryImport ImportUserDictionary(std::u16string_view)
{
    return {};
}

std::u16string ExportUserDictionary(const std::vector<UserDictionary::Word>&, UserDictionaryFormat)
{
    return {};
}

TextEncoding ExportEncoding(UserDictionaryFormat)
{
    return TextEncoding::Utf8;
}

} // namespace astelio
