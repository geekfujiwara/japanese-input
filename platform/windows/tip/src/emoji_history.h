#pragma once

#include <string>
#include <vector>

namespace astelio::tip {

// B-13: the recently used emoji, shared by every app of the user through
// "%LOCALAPPDATA%\AstelioIME\emoji_recent.txt" (UTF-8, one emoji per line, newest first).
std::vector<std::u16string> LoadRecentEmoji();
void SaveRecentEmoji(const std::vector<std::u16string>& recent);

// Test hook: use `path` instead (nullptr restores the default).
void UseRecentEmojiFile(const wchar_t* path);

} // namespace astelio::tip
