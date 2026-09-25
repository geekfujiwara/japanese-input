#pragma once

#include <windows.h>

#include <msctf.h>

namespace astelio::tip {

// How the uncommitted text is underlined (B-02): typed kana, converted segments, and the focused segment.
enum class DisplayAttribute : int {
    Input = 0,
    Converted = 1,
    Focused = 2,
};
inline constexpr int kDisplayAttributeCount = 3;

const GUID& DisplayAttributeGuid(DisplayAttribute attribute);

// ITfDisplayAttributeProvider implementation (the text service forwards to these).
HRESULT EnumDisplayAttributes(IEnumTfDisplayAttributeInfo** attributes);
HRESULT GetDisplayAttribute(REFGUID guid, ITfDisplayAttributeInfo** attribute);

} // namespace astelio::tip
