#include "astelio/input_session.h"

namespace astelio {

InputSession::InputSession(const RomajiTable& table, CharacterSettings settings)
    : composer_(table, settings), settings_(settings)
{
}

SessionOutput InputSession::SetJapaneseMode(bool enabled)
{
    SessionOutput output;
    if (!enabled && Composing()) {
        output.commit = composer_.Commit();
        output.composition_changed = true;
    }
    japanese_mode_ = enabled;
    return output;
}

bool InputSession::WillHandle(const KeyEvent& key) const
{
    if (!japanese_mode_) {
        return false;
    }
    switch (key.kind) {
    case KeyKind::Character:
    case KeyKind::Space:
        return true;
    case KeyKind::Enter:
    case KeyKind::Escape:
    case KeyKind::Backspace:
    case KeyKind::Delete:
    case KeyKind::Left:
    case KeyKind::Right:
        return Composing();
    }
    return false;
}

SessionOutput InputSession::Handle(const KeyEvent& key)
{
    SessionOutput output;
    if (!WillHandle(key)) {
        return output;
    }
    output.composition_changed = true;
    switch (key.kind) {
    case KeyKind::Character:
        composer_.InsertKey(key.character);
        break;
    case KeyKind::Space:
        // Space converts while composing (phase 3); until then it keeps the text unchanged.
        if (Composing()) {
            output.composition_changed = false;
        } else {
            output.commit = SpaceCharacter(settings_);
            output.composition_changed = false;
        }
        break;
    case KeyKind::Enter:
        output.commit = composer_.Commit();
        break;
    case KeyKind::Escape:
        composer_.Clear();
        break;
    case KeyKind::Backspace:
        composer_.Backspace();
        break;
    case KeyKind::Delete:
        composer_.Delete();
        break;
    case KeyKind::Left:
        composer_.MoveLeft();
        break;
    case KeyKind::Right:
        composer_.MoveRight();
        break;
    }
    return output;
}

} // namespace astelio
