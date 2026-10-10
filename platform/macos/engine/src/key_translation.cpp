#include "astelio/mac/key_translation.h"

namespace astelio::mac {
namespace {

struct KeyCodeKey {
    std::uint16_t key_code;
    char16_t normal;
    char16_t shifted;
};

// kVK_ANSI_* of the US layout. 0x0A (kVK_ISO_Section) is the extra key of ISO keyboards, which "ABC" reads as `.
constexpr KeyCodeKey kUsLayout[] = {
    {0x32, u'`', u'~'},  {0x12, u'1', u'!'},  {0x13, u'2', u'@'},  {0x14, u'3', u'#'},  {0x15, u'4', u'$'},
    {0x17, u'5', u'%'},  {0x16, u'6', u'^'},  {0x1A, u'7', u'&'},  {0x1C, u'8', u'*'},  {0x19, u'9', u'('},
    {0x1D, u'0', u')'},  {0x1B, u'-', u'_'},  {0x18, u'=', u'+'},  {0x0C, u'q', u'Q'},  {0x0D, u'w', u'W'},
    {0x0E, u'e', u'E'},  {0x0F, u'r', u'R'},  {0x11, u't', u'T'},  {0x10, u'y', u'Y'},  {0x20, u'u', u'U'},
    {0x22, u'i', u'I'},  {0x1F, u'o', u'O'},  {0x23, u'p', u'P'},  {0x21, u'[', u'{'},  {0x1E, u']', u'}'},
    {0x2A, u'\\', u'|'}, {0x00, u'a', u'A'},  {0x01, u's', u'S'},  {0x02, u'd', u'D'},  {0x03, u'f', u'F'},
    {0x05, u'g', u'G'},  {0x04, u'h', u'H'},  {0x26, u'j', u'J'},  {0x28, u'k', u'K'},  {0x25, u'l', u'L'},
    {0x29, u';', u':'},  {0x27, u'\'', u'"'}, {0x06, u'z', u'Z'},  {0x07, u'x', u'X'},  {0x08, u'c', u'C'},
    {0x09, u'v', u'V'},  {0x0B, u'b', u'B'},  {0x2D, u'n', u'N'},  {0x2E, u'm', u'M'},  {0x2B, u',', u'<'},
    {0x2F, u'.', u'>'},  {0x2C, u'/', u'?'},  {0x0A, u'`', u'~'},
};

enum : std::uint16_t {
    kReturn = 0x24,
    kTab = 0x30,
    kSpace = 0x31,
    kBackspace = 0x33,
    kEscape = 0x35,
    kKeypadEnter = 0x4C,
    kF6 = 0x61,
    kF7 = 0x62,
    kF8 = 0x64,
    kF9 = 0x65,
    kF10 = 0x6D,
    kPageUp = 0x74,
    kForwardDelete = 0x75,
    kPageDown = 0x79,
    kLeft = 0x7B,
    kRight = 0x7C,
    kDown = 0x7D,
    kUp = 0x7E,
};

std::optional<char16_t> KeypadCharacter(std::uint16_t key_code)
{
    switch (key_code) {
    case 0x52: return u'0';
    case 0x53: return u'1';
    case 0x54: return u'2';
    case 0x55: return u'3';
    case 0x56: return u'4';
    case 0x57: return u'5';
    case 0x58: return u'6';
    case 0x59: return u'7';
    case 0x5B: return u'8';
    case 0x5C: return u'9';
    case 0x41: return u'.';
    case 0x43: return u'*';
    case 0x45: return u'+';
    case 0x4E: return u'-';
    case 0x4B: return u'/';
    default: return std::nullopt;
    }
}

bool IsLetter(char16_t c)
{
    return c >= u'a' && c <= u'z';
}

} // namespace

std::optional<KeyEvent> TranslateKey(std::uint16_t key_code, const Modifiers& modifiers)
{
    // D-05 / B-06 / B-08: Control+Delete, Control+Down and Control+Backspace reach the session, which uses them only
    // while they mean something (candidate list, conversion, right after a commit).
    if (modifiers.control && !modifiers.option && !modifiers.command) {
        KeyEvent key{KeyKind::Delete, 0};
        key.control = true;
        switch (key_code) {
        case kForwardDelete: return key;
        case kDown: key.kind = KeyKind::Down; return key;
        case kBackspace: key.kind = KeyKind::Backspace; return key;
        default: return std::nullopt;
        }
    }
    if (modifiers.control || modifiers.option || modifiers.command) {
        return std::nullopt;
    }

    switch (key_code) {
    case kSpace: return KeyEvent{KeyKind::Space, 0};
    case kReturn:
    case kKeypadEnter: return KeyEvent{KeyKind::Enter, 0};
    case kEscape: return KeyEvent{KeyKind::Escape, 0};
    case kBackspace: return KeyEvent{KeyKind::Backspace, 0};
    case kForwardDelete: return KeyEvent{KeyKind::Delete, 0};
    case kLeft: return KeyEvent{KeyKind::Left, 0, modifiers.shift};
    case kRight: return KeyEvent{KeyKind::Right, 0, modifiers.shift};
    case kUp: return KeyEvent{KeyKind::Up, 0, modifiers.shift};
    case kDown: return KeyEvent{KeyKind::Down, 0, modifiers.shift};
    case kPageUp: return KeyEvent{KeyKind::PageUp, 0, modifiers.shift};
    case kPageDown: return KeyEvent{KeyKind::PageDown, 0, modifiers.shift};
    case kTab: return KeyEvent{KeyKind::Tab, 0, modifiers.shift};
    case kF6: return KeyEvent{KeyKind::F6, 0};
    case kF7: return KeyEvent{KeyKind::F7, 0};
    case kF8: return KeyEvent{KeyKind::F8, 0};
    case kF9: return KeyEvent{KeyKind::F9, 0};
    case kF10: return KeyEvent{KeyKind::F10, 0};
    default: break;
    }

    if (const std::optional<char16_t> keypad = KeypadCharacter(key_code)) {
        return KeyEvent{KeyKind::Character, *keypad};
    }
    if (modifiers.function) {
        return std::nullopt; // Fn with a letter or digit is a system shortcut
    }
    for (const KeyCodeKey& key : kUsLayout) {
        if (key.key_code != key_code) {
            continue;
        }
        const bool upper = IsLetter(key.normal) ? modifiers.shift != modifiers.caps_lock : modifiers.shift;
        return KeyEvent{KeyKind::Character, upper ? key.shifted : key.normal};
    }
    return std::nullopt;
}

} // namespace astelio::mac
