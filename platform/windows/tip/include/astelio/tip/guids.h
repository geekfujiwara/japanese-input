#pragma once

#include <guiddef.h>

namespace astelio::tip {

// {C1048430-24FC-43BE-9B5B-2B84631D66E2}
inline constexpr CLSID kTextServiceClsid = {
    0xc1048430, 0x24fc, 0x43be, {0x9b, 0x5b, 0x2b, 0x84, 0x63, 0x1d, 0x66, 0xe2}};

// {2511108E-04A3-4E7C-9817-BEBCC6A68A2F}
inline constexpr GUID kJapaneseProfileGuid = {
    0x2511108e, 0x04a3, 0x4e7c, {0x98, 0x17, 0xbe, 0xbc, 0xc6, 0xa6, 0x8a, 0x2f}};

inline constexpr unsigned short kJapaneseLangId = 0x0411;

// GUID_LBI_INPUTMODE: the language bar item the taskbar shows as the input mode indicator.
inline constexpr GUID kLangBarInputModeGuid = {
    0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};

} // namespace astelio::tip
