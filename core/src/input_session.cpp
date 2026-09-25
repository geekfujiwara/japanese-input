#include "astelio/input_session.h"

#include <algorithm>
#include <utility>

namespace astelio {

InputSession::InputSession(const RomajiTable& table, CharacterSettings settings)
    : composer_(table, settings), settings_(settings)
{
}

SessionOutput InputSession::SetJapaneseMode(bool enabled)
{
    SessionOutput output;
    if (!enabled && converting_) {
        output.commit = ConvertedText();
        output.composition_changed = true;
        EndConversion();
    } else if (!enabled && Composing()) {
        output.commit = composer_.Commit();
        output.composition_changed = true;
    }
    japanese_mode_ = enabled;
    predictions_.clear();
    return output;
}

void InputSession::AbandonComposition()
{
    composer_.Clear();
    EndConversion();
    predictions_.clear();
}

std::u16string InputSession::CompositionText() const
{
    return converting_ ? ConvertedText() : composer_.Text();
}

std::size_t InputSession::CompositionCursor() const
{
    return converting_ ? ConvertedText().size() : composer_.Cursor();
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
    case KeyKind::Up:
    case KeyKind::Down:
    case KeyKind::PageUp:
    case KeyKind::PageDown:
    case KeyKind::Tab:
        return Composing();
    }
    return false;
}

SessionOutput InputSession::Handle(const KeyEvent& key)
{
    if (!WillHandle(key)) {
        return {};
    }
    SessionOutput output = converting_ ? HandleConversion(key) : HandleComposition(key);
    UpdatePredictions();
    return output;
}

void InputSession::UpdatePredictions()
{
    predictions_.clear();
    if (converting_ || converter_ == nullptr || composer_.Empty()) {
        return;
    }
    // Romaji still being typed ("arig") is not part of the reading yet.
    std::u16string reading = composer_.Text();
    while (!reading.empty() && reading.back() >= u'a' && reading.back() <= u'z') {
        reading.pop_back();
    }
    if (reading.size() >= kMinPredictionLength) {
        predictions_ = converter_->Predict(reading, kCandidatePageSize);
    }
}

void InputSession::StartPrediction()
{
    ConvertedSegment segment;
    segment.candidates = predictions_;
    reading_ = composer_.Commit();
    segment.reading = reading_;
    for (std::u16string form : {reading_, HiraganaToKatakana(reading_)}) {
        if (std::find(segment.candidates.begin(), segment.candidates.end(), form) == segment.candidates.end()) {
            segment.candidates.push_back(std::move(form));
        }
    }
    segments_ = {std::move(segment)};
    selected_ = {0};
    focus_ = 0;
    converting_ = true;
    candidate_list_visible_ = true;
}

SessionOutput InputSession::HandleComposition(const KeyEvent& key)
{
    SessionOutput output;
    output.composition_changed = true;
    switch (key.kind) {
    case KeyKind::Character:
        composer_.InsertKey(key.character);
        break;
    case KeyKind::Space:
        if (!Composing()) {
            output.commit = SpaceCharacter(settings_);
            output.composition_changed = false;
        } else if (converter_ == nullptr) {
            output.composition_changed = false;
        } else {
            reading_ = composer_.Commit();
            Convert({});
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
    case KeyKind::Up:
    case KeyKind::PageUp:
    case KeyKind::PageDown:
        output.composition_changed = false;
        break;
    case KeyKind::Down:
    case KeyKind::Tab:
        if (predictions_.empty()) {
            output.composition_changed = false;
        } else {
            StartPrediction();
        }
        break;
    }
    return output;
}

bool InputSession::HandleCandidateList(const KeyEvent& key)
{
    const std::size_t count = segments_[focus_].candidates.size();
    std::size_t& selected = selected_[focus_];
    switch (key.kind) {
    case KeyKind::Space:
    case KeyKind::Down:
        selected = (selected + 1) % count;
        break;
    case KeyKind::Up:
        selected = (selected + count - 1) % count;
        break;
    case KeyKind::Tab:
        selected = key.shift ? (selected + count - 1) % count : (selected + 1) % count;
        break;
    case KeyKind::PageDown:
        selected = std::min(selected + kCandidatePageSize, count - 1);
        break;
    case KeyKind::PageUp:
        selected = selected >= kCandidatePageSize ? selected - kCandidatePageSize : 0;
        break;
    case KeyKind::Character: {
        // 1-9 pick from the current page and close the list.
        if (!candidate_list_visible_ || key.character < u'1' || key.character > u'9') {
            return false;
        }
        const std::size_t index =
            selected / kCandidatePageSize * kCandidatePageSize + static_cast<std::size_t>(key.character - u'1');
        if (index < count) {
            selected = index;
        }
        candidate_list_visible_ = false;
        return true;
    }
    default:
        return false;
    }
    candidate_list_visible_ = true;
    return true;
}

SessionOutput InputSession::HandleConversion(const KeyEvent& key)
{
    SessionOutput output;
    output.composition_changed = true;
    if (HandleCandidateList(key)) {
        return output;
    }
    candidate_list_visible_ = false;
    switch (key.kind) {
    case KeyKind::Space:
    case KeyKind::Down:
    case KeyKind::Up:
    case KeyKind::PageUp:
    case KeyKind::PageDown:
    case KeyKind::Tab:
        break;
    case KeyKind::Left:
    case KeyKind::Right: {
        const bool forward = key.kind == KeyKind::Right;
        if (!key.shift) {
            if (forward && focus_ + 1 < segments_.size()) {
                ++focus_;
            } else if (!forward && focus_ > 0) {
                --focus_;
            }
            break;
        }
        // Resize the focused segment and convert the rest again, keeping the choices before it.
        std::vector<std::size_t> fixed;
        std::size_t before = 0;
        for (std::size_t i = 0; i < focus_; ++i) {
            fixed.push_back(segments_[i].reading.size());
            before += fixed.back();
        }
        const std::size_t length = segments_[focus_].reading.size();
        const std::size_t resized = forward ? length + 1 : length - 1;
        if (resized == 0 || before + resized > reading_.size()) {
            output.composition_changed = false;
            break;
        }
        fixed.push_back(resized);
        std::vector<std::u16string> chosen;
        for (std::size_t i = 0; i < focus_; ++i) {
            chosen.push_back(segments_[i].candidates[selected_[i]]);
        }
        Convert(std::move(fixed));
        for (std::size_t i = 0; i < chosen.size() && i < segments_.size(); ++i) {
            const std::vector<std::u16string>& candidates = segments_[i].candidates;
            for (std::size_t c = 0; c < candidates.size(); ++c) {
                if (candidates[c] == chosen[i]) {
                    selected_[i] = c;
                    break;
                }
            }
        }
        break;
    }
    case KeyKind::Enter:
        output.commit = ConvertedText();
        EndConversion();
        break;
    case KeyKind::Escape:
    case KeyKind::Backspace:
        // Back to the kana before conversion.
        composer_.SetText(reading_);
        EndConversion();
        break;
    case KeyKind::Character:
        output.commit = ConvertedText();
        EndConversion();
        composer_.InsertKey(key.character);
        break;
    case KeyKind::Delete:
        output.composition_changed = false;
        break;
    }
    return output;
}

void InputSession::Convert(std::vector<std::size_t> fixed_lengths)
{
    segments_ = converter_->Convert(reading_, fixed_lengths);
    selected_.assign(segments_.size(), 0);
    if (segments_.empty()) {
        composer_.SetText(reading_);
        EndConversion();
        return;
    }
    converting_ = true;
    if (focus_ >= segments_.size()) {
        focus_ = segments_.size() - 1;
    }
}

std::u16string InputSession::ConvertedText() const
{
    std::u16string text;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        text += segments_[i].candidates[selected_[i]];
    }
    return text;
}

void InputSession::EndConversion()
{
    converting_ = false;
    reading_.clear();
    segments_.clear();
    selected_.clear();
    focus_ = 0;
    candidate_list_visible_ = false;
}

} // namespace astelio
