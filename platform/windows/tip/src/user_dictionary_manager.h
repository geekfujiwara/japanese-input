#pragma once

namespace astelio::tip {

// D-02 / D-03: a window to add, edit, delete and search the user's words, and to import and export them. Opened
// from the menu of the input mode button in the taskbar. One window per process; opening it again brings it to
// the front. Changes are saved at once, and every app uses them from its next input.
void ShowUserDictionaryManager();

} // namespace astelio::tip
