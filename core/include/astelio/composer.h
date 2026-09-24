#pragma once

#include "astelio/character_rules.h"
#include "astelio/romaji_table.h"

#include <cstddef>
#include <string>

namespace astelio {

// Uncommitted text being typed, before kana-kanji conversion.
// Keys are the characters a US keyboard produces (Shift already applied).
class Composer {
public:
    // `table` must outlive the composer.
    Composer(const RomajiTable& table, CharacterSettings settings);

    void InsertKey(char16_t key);
    void Backspace();
    void Delete();
    void MoveLeft();
    void MoveRight();

    // Shift+letter starts temporary alphanumeric mode; it ends on commit, clear, or this call (Shift tap).
    void ExitTemporaryAlphanumeric();
    bool IsTemporaryAlphanumeric() const { return temporary_alphanumeric_; }

    std::u16string Text() const;
    std::size_t Cursor() const;
    bool Empty() const;

    // Resolves pending romaji, returns the whole text, and resets.
    std::u16string Commit();
    void Clear();

private:
    void ResolvePending(bool flush);

    const RomajiTable* table_;
    CharacterSettings settings_;
    std::u16string before_;  // text left of the cursor
    std::u16string pending_; // romaji not yet converted, at the cursor
    std::u16string after_;   // text right of the cursor
    bool temporary_alphanumeric_ = false;
};

} // namespace astelio
