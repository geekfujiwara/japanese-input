#pragma once

#include "astelio/converter.h"
#include "astelio/emoji.h"

namespace astelio::tip {

// B-13: the emoji of the dictionary behind `converter`, built on first use. nullptr for an unknown converter.
const EmojiCatalog* EmojiCatalogFor(const Converter* converter);

// The system dictionary shared by every text service in the process, mapped read-only from
// "<install dir>\dictionary\system.dic". Returns nullptr when it is missing or invalid (typing still works).
const Converter* SharedConverter();

// Test hook: maps `path` instead of the installed dictionary. Returns the converter or nullptr.
const Converter* UseDictionaryFile(const wchar_t* path);

// Unmaps the dictionaries when the DLL is unloaded.
void ReleaseDictionaries();

} // namespace astelio::tip
