#pragma once

#include <windows.h>

namespace astelio::tip {

// Control ids of the window (the tests find the fields by them).
enum UserDictionaryControl : int {
    kUserDictionarySearchId = 100,
    kUserDictionaryListId,
    kUserDictionaryReadingId,
    kUserDictionarySurfaceId,
    kUserDictionaryPosId,
    kUserDictionaryCommentId,
};

// D-02 / D-03: a window to add, edit, delete and search the user's words, and to import and export them. Opened
// from the settings app. One window per process; opening it again brings it to
// the front. Changes are saved at once, and every app uses them from its next input.
void ShowUserDictionaryManager();
// The open window, or nullptr.
HWND UserDictionaryManagerWindow();

} // namespace astelio::tip
