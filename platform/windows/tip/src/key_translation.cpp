#include "key_translation.h"

#include <windows.h>

namespace astelio::tip {
namespace {

struct ScanCodeKey {
    std::uint8_t scan_code;
    char16_t normal;
    char16_t shifted;
};

// US ANSI layout, plus 0x56 which ISO keyboards send for the extra backslash key.
constexpr ScanCodeKey kUsLayout[] = {
    {0x29, u'`', u'~'},  {0x02, u'1', u'!'},  {0x03, u'2', u'@'},  {0x04, u'3', u'#'},  {0x05, u'4', u'$'},
    {0x06, u'5', u'%'},  {0x07, u'6', u'^'},  {0x08, u'7', u'&'},  {0x09, u'8', u'*'},  {0x0A, u'9', u'('},
    {0x0B, u'0', u')'},  {0x0C, u'-', u'_'},  {0x0D, u'=', u'+'},  {0x10, u'q', u'Q'},  {0x11, u'w', u'W'},
    {0x12, u'e', u'E'},  {0x13, u'r', u'R'},  {0x14, u't', u'T'},  {0x15, u'y', u'Y'},  {0x16, u'u', u'U'},
    {0x17, u'i', u'I'},  {0x18, u'o', u'O'},  {0x19, u'p', u'P'},  {0x1A, u'[', u'{'},  {0x1B, u']', u'}'},
    {0x2B, u'\\', u'|'}, {0x1E, u'a', u'A'},  {0x1F, u's', u'S'},  {0x20, u'd', u'D'},  {0x21, u'f', u'F'},
    {0x22, u'g', u'G'},  {0x23, u'h', u'H'},  {0x24, u'j', u'J'},  {0x25, u'k', u'K'},  {0x26, u'l', u'L'},
    {0x27, u';', u':'},  {0x28, u'\'', u'"'}, {0x2C, u'z', u'Z'},  {0x2D, u'x', u'X'},  {0x2E, u'c', u'C'},
    {0x2F, u'v', u'V'},  {0x30, u'b', u'B'},  {0x31, u'n', u'N'},  {0x32, u'm', u'M'},  {0x33, u',', u'<'},
    {0x34, u'.', u'>'},  {0x35, u'/', u'?'},  {0x56, u'\\', u'|'},
};

std::optional<char16_t> NumpadCharacter(std::uint32_t virtual_key)
{
    if (virtual_key >= VK_NUMPAD0 && virtual_key <= VK_NUMPAD9) {
        return static_cast<char16_t>(u'0' + (virtual_key - VK_NUMPAD0));
    }
    switch (virtual_key) {
    case VK_MULTIPLY: return u'*';
    case VK_ADD: return u'+';
    case VK_SUBTRACT: return u'-';
    case VK_DIVIDE: return u'/';
    case VK_DECIMAL: return u'.';
    default: return std::nullopt;
    }
}

bool IsLetter(char16_t c)
{
    return c >= u'a' && c <= u'z';
}

} // namespace

std::optional<KeyEvent> TranslateKey(std::uint32_t virtual_key, std::uint32_t lparam, const Modifiers& modifiers)
{
    if (modifiers.control || modifiers.alt || modifiers.windows) {
        return std::nullopt;
    }

    switch (virtual_key) {
    case VK_SPACE: return KeyEvent{KeyKind::Space, 0};
    case VK_RETURN: return KeyEvent{KeyKind::Enter, 0};
    case VK_ESCAPE: return KeyEvent{KeyKind::Escape, 0};
    case VK_BACK: return KeyEvent{KeyKind::Backspace, 0};
    case VK_DELETE: return KeyEvent{KeyKind::Delete, 0};
    case VK_LEFT: return KeyEvent{KeyKind::Left, 0, modifiers.shift};
    case VK_RIGHT: return KeyEvent{KeyKind::Right, 0, modifiers.shift};
    case VK_UP: return KeyEvent{KeyKind::Up, 0, modifiers.shift};
    case VK_DOWN: return KeyEvent{KeyKind::Down, 0, modifiers.shift};
    case VK_PRIOR: return KeyEvent{KeyKind::PageUp, 0, modifiers.shift};
    case VK_NEXT: return KeyEvent{KeyKind::PageDown, 0, modifiers.shift};
    case VK_TAB: return KeyEvent{KeyKind::Tab, 0, modifiers.shift};
    default: break;
    }

    if (const std::optional<char16_t> numpad = NumpadCharacter(virtual_key)) {
        return KeyEvent{KeyKind::Character, *numpad};
    }

    const bool extended = (lparam & (1u << 24)) != 0;
    const auto scan_code = static_cast<std::uint8_t>((lparam >> 16) & 0xFF);
    if (extended) {
        return std::nullopt;
    }
    for (const ScanCodeKey& key : kUsLayout) {
        if (key.scan_code != scan_code) {
            continue;
        }
        const bool upper = IsLetter(key.normal) ? modifiers.shift != modifiers.caps_lock : modifiers.shift;
        return KeyEvent{KeyKind::Character, upper ? key.shifted : key.normal};
    }
    return std::nullopt;
}

} // namespace astelio::tip
