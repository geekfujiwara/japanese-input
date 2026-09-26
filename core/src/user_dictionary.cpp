#include "astelio/user_dictionary.h"

namespace astelio {

bool UserDictionary::Add(Word)
{
    return false;
}

bool UserDictionary::Update(const Word&, Word)
{
    return false;
}

bool UserDictionary::Remove(const Word&)
{
    return false;
}

void UserDictionary::Clear()
{
    words_.clear();
}

std::vector<UserDictionary::Word> UserDictionary::Search(std::u16string_view) const
{
    return {};
}

std::vector<std::u16string> UserDictionary::Lookup(std::u16string_view) const
{
    return {};
}

std::vector<std::u16string> UserDictionary::Predict(std::u16string_view, std::size_t) const
{
    return {};
}

bool UserDictionary::Suppressed(std::u16string_view, std::u16string_view) const
{
    return false;
}

std::string UserDictionary::Serialize() const
{
    return {};
}

UserDictionary UserDictionary::Parse(std::string_view)
{
    return {};
}

} // namespace astelio
