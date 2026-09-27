#include "dialog_keys.h"

#include <commctrl.h>

#include <iterator>

namespace astelio::tip {
namespace {

constexpr UINT_PTR kSubclassId = 0x4153; // "AS"

bool IsButton(HWND control)
{
    wchar_t name[16] = {};
    GetClassNameW(control, name, static_cast<int>(std::size(name)));
    return CompareStringOrdinal(name, -1, L"Button", -1, TRUE) == CSTR_EQUAL;
}

bool IsDroppedCombo(HWND control)
{
    return SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0) != FALSE;
}

LRESULT CALLBACK KeyProc(HWND control, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR /*id*/, DWORD_PTR data)
{
    const auto top = reinterpret_cast<HWND>(data);
    switch (message) {
    case WM_KEYDOWN:
        switch (wparam) {
        case VK_TAB:
            if (const HWND next = GetNextDlgTabItem(top, control, GetKeyState(VK_SHIFT) < 0)) {
                SetFocus(next);
            }
            return 0;
        case VK_RETURN:
            if (IsDroppedCombo(control)) {
                break;
            }
            if (IsButton(control)) {
                SendMessageW(control, BM_CLICK, 0, 0);
            } else {
                SendMessageW(top, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            }
            return 0;
        case VK_ESCAPE:
            if (IsDroppedCombo(control)) {
                break;
            }
            SendMessageW(top, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            return 0;
        default:
            break;
        }
        break;
    case WM_CHAR:
        if (wparam == L'\t' || wparam == L'\r' || wparam == 0x1B) {
            return 0; // an edit control would beep
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, KeyProc, kSubclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

BOOL CALLBACK Subclass(HWND control, LPARAM top)
{
    if ((GetWindowLongW(control, GWL_STYLE) & WS_TABSTOP) != 0) {
        SetWindowSubclass(control, KeyProc, kSubclassId, static_cast<DWORD_PTR>(top));
    }
    return TRUE;
}

} // namespace

void UseDialogKeys(HWND top)
{
    EnumChildWindows(top, Subclass, reinterpret_cast<LPARAM>(top));
}

} // namespace astelio::tip
