#pragma once

#include <windows.h>

namespace astelio::tip {

// The history and user dictionary windows run in the app's message loop, which does not call IsDialogMessage.
// This makes Tab / Shift+Tab move between the controls of `top` that have WS_TABSTOP, Enter send IDOK (or click
// the focused button) and Esc send IDCANCEL to `top`. Keys the IME takes while composing never get here.
void UseDialogKeys(HWND top);

} // namespace astelio::tip
