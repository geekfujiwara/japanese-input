#pragma once

#include <windows.h>

namespace astelio::tip {

// D-05: a window that lists the history of chosen words, to delete some of them or all. Opened from the settings
// app. One window per process; opening it again brings it to the front.
void ShowLearningManager();
// The open window, or nullptr.
HWND LearningManagerWindow();

} // namespace astelio::tip
