#pragma once

#include "astelio/learning.h"

#include <cstdint>
#include <string>

namespace astelio::tip {

// D-04: the history of chosen words, in %LOCALAPPDATA%\AstelioIME\learning.tsv (only this user can read it).
// Every app loads the TIP, so the file is shared: it is written whole through a temporary file, and reloaded
// when another app changed it.

// Tests use their own file; nullptr goes back to the default.
void UseLearningFile(const wchar_t* path);
// Tests use their own key under HKCU for the settings below; nullptr goes back to Software\AstelioIME.
void UseSettingsKey(const wchar_t* key);
LearningHistory LoadLearning();
void SaveLearning(const LearningHistory& history);
// The file's last write time (0 when it is missing), to notice changes made by other apps.
std::uint64_t LearningFileStamp();

// Whether the history is used (HKCU\Software\AstelioIME, LearningEnabled; on unless set to 0).
bool LearningEnabled();
void SetLearningEnabled(bool enabled);

// D-06: secret mode (LearningPaused, off unless set to 1): the history is used, but nothing new is recorded.
bool LearningPaused();
void SetLearningPaused(bool paused);

// D-06: apps whose typing is never recorded (NoLearningApps: exe file names in lower case).
bool AppLearningExcluded(const std::wstring& app);
void SetAppLearningExcluded(const std::wstring& app, bool excluded);

// B-14: whether もしかして words are offered (TypoSuggestions under the same key; on unless set to 0).
bool TypoSuggestionsEnabled();

} // namespace astelio::tip
