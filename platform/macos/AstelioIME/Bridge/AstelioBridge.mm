#import "AstelioBridge.h"

#include "astelio/mac/engine.h"
#include "astelio/mac/mac_session.h"

#include <memory>
#include <optional>
#include <string>

namespace {

NSString* ToNSString(const std::u16string& text)
{
    return [NSString stringWithCharacters:reinterpret_cast<const unichar*>(text.data()) length:text.size()];
}

NSArray<NSString*>* ToNSArray(const std::vector<std::u16string>& items)
{
    NSMutableArray<NSString*>* array = [NSMutableArray arrayWithCapacity:items.size()];
    for (const std::u16string& item : items) {
        [array addObject:ToNSString(item)];
    }
    return array;
}

NSInteger IndexOrNone(std::size_t index)
{
    return index == astelio::kNoEmojiSelection ? -1 : static_cast<NSInteger>(index);
}

std::filesystem::path ToPath(NSString* path)
{
    return path.length > 0 ? std::filesystem::path(path.fileSystemRepresentation) : std::filesystem::path();
}

} // namespace

@interface ASTSpan ()
- (instancetype)initWithRange:(NSRange)range style:(ASTSpanStyle)style;
@end

@interface ASTComposition ()
- (instancetype)initWithComposition:(const astelio::mac::Composition&)composition;
@end

@interface ASTCandidateView ()
- (instancetype)initWithView:(const astelio::mac::CandidateView&)view;
@end

@interface ASTOutput ()
- (instancetype)initWithOutput:(const astelio::SessionOutput&)output;
@end

@interface ASTSettings ()
- (astelio::mac::Settings)settings;
@end

@interface ASTEngine ()
- (astelio::mac::Engine&)engine;
@end

@implementation ASTSpan
- (instancetype)initWithRange:(NSRange)range style:(ASTSpanStyle)style
{
    if ((self = [super init])) {
        _range = range;
        _style = style;
    }
    return self;
}
@end

@implementation ASTComposition
- (instancetype)initWithComposition:(const astelio::mac::Composition&)composition
{
    if ((self = [super init])) {
        _text = ToNSString(composition.text);
        _cursor = static_cast<NSInteger>(composition.cursor);
        _anchor = static_cast<NSInteger>(composition.anchor);
        NSMutableArray<ASTSpan*>* spans = [NSMutableArray array];
        for (const astelio::mac::Span& span : composition.spans) {
            ASTSpanStyle style = ASTSpanStyleInput;
            if (span.style == astelio::mac::SpanStyle::Converted) {
                style = ASTSpanStyleConverted;
            } else if (span.style == astelio::mac::SpanStyle::Focused) {
                style = ASTSpanStyleFocused;
            }
            [spans addObject:[[ASTSpan alloc] initWithRange:NSMakeRange(span.start, span.length) style:style]];
        }
        _spans = spans;
    }
    return self;
}
@end

@implementation ASTCandidateView
- (instancetype)initWithView:(const astelio::mac::CandidateView&)view
{
    if ((self = [super init])) {
        switch (view.kind) {
        case astelio::mac::CandidateKind::Hidden: _kind = ASTCandidateKindHidden; break;
        case astelio::mac::CandidateKind::Predictions: _kind = ASTCandidateKindPredictions; break;
        case astelio::mac::CandidateKind::Candidates: _kind = ASTCandidateKindCandidates; break;
        case astelio::mac::CandidateKind::Emoji: _kind = ASTCandidateKindEmoji; break;
        }
        _items = ToNSArray(view.items);
        NSMutableArray<NSNumber*>* marks = [NSMutableArray arrayWithCapacity:view.marks.size()];
        for (astelio::mac::CandidateMark mark : view.marks) {
            ASTCandidateMark value = ASTCandidateMarkPlain;
            if (mark == astelio::mac::CandidateMark::Learned) {
                value = ASTCandidateMarkLearned;
            } else if (mark == astelio::mac::CandidateMark::Typo) {
                value = ASTCandidateMarkTypo;
            }
            [marks addObject:@(value)];
        }
        _marks = marks;
        _selected = IndexOrNone(view.selected);
        _firstIndex = static_cast<NSInteger>(view.first_index);
        _total = static_cast<NSInteger>(view.total);
        _columns = static_cast<NSInteger>(view.columns);
        _title = ToNSString(view.title);
        _related = ToNSArray(view.related);
        _relatedSelected = IndexOrNone(view.related_selected);
    }
    return self;
}
@end

@implementation ASTOutput
- (instancetype)initWithOutput:(const astelio::SessionOutput&)output
{
    if ((self = [super init])) {
        _commit = ToNSString(output.commit);
        _undoCommit = ToNSString(output.undo_commit);
        _compositionChanged = output.composition_changed;
    }
    return self;
}
@end

@implementation ASTSettings
- (instancetype)init
{
    if ((self = [super init])) {
        const astelio::mac::Settings defaults;
        _tapModifier = ASTTapModifierCommand;
        _learningEnabled = defaults.learning_enabled;
        _learningPaused = defaults.learning_paused;
        _typoSuggestions = defaults.typo_suggestions;
        _listNumberPeriod = defaults.list_number_period;
        _autoCloseBrackets = defaults.auto_close_brackets;
        _dateFormat = static_cast<NSInteger>(defaults.date_format);
    }
    return self;
}

- (astelio::mac::Settings)settings
{
    astelio::mac::Settings settings;
    settings.tap_modifier = _tapModifier == ASTTapModifierOption ? astelio::mac::TapModifier::Option
                                                                 : astelio::mac::TapModifier::Command;
    settings.learning_enabled = _learningEnabled;
    settings.learning_paused = _learningPaused;
    settings.typo_suggestions = _typoSuggestions;
    settings.list_number_period = _listNumberPeriod;
    settings.auto_close_brackets = _autoCloseBrackets;
    if (_dateFormat >= 0 && static_cast<std::size_t>(_dateFormat) < astelio::kDateFormatNames.size()) {
        settings.date_format = static_cast<astelio::DateFormat>(_dateFormat);
    }
    return settings;
}
@end

@implementation ASTEngine {
    std::unique_ptr<astelio::mac::Engine> _engine;
}

- (instancetype)initWithDictionaryPath:(NSString*)dictionaryPath dataDirectory:(NSString*)dataDirectory
{
    if ((self = [super init])) {
        _dataDirectory = [dataDirectory copy];
        _engine = std::make_unique<astelio::mac::Engine>(ToPath(dictionaryPath), ToPath(dataDirectory));
    }
    return self;
}

- (BOOL)hasDictionary
{
    return _engine->dictionary() != nullptr;
}

- (BOOL)clearLearning
{
    return _engine->ClearLearning();
}

- (astelio::mac::Engine&)engine
{
    return *_engine;
}
@end

@implementation ASTSession {
    ASTEngine* _owner; // keeps the engine alive while the session points at it
    std::unique_ptr<astelio::mac::MacSession> _session;
}

- (instancetype)initWithEngine:(ASTEngine*)engine
{
    if ((self = [super init])) {
        _owner = engine;
        _session = std::make_unique<astelio::mac::MacSession>([engine engine]);
    }
    return self;
}

- (void)applySettings:(ASTSettings*)settings
{
    _session->ApplySettings([settings settings]);
}

- (void)setRecordingAllowed:(BOOL)allowed
{
    _session->SetRecordingAllowed(allowed);
}

- (BOOL)japaneseMode
{
    return _session->JapaneseMode();
}

- (ASTOutput*)changeModeToJapanese:(BOOL)japanese
{
    return [[ASTOutput alloc] initWithOutput:_session->SetJapaneseMode(japanese)];
}

- (ASTOutput*)keyDownWithKeyCode:(uint16_t)keyCode
                           shift:(BOOL)shift
                         control:(BOOL)control
                          option:(BOOL)option
                         command:(BOOL)command
                        capsLock:(BOOL)capsLock
                        function:(BOOL)function
{
    astelio::mac::Modifiers modifiers;
    modifiers.shift = shift;
    modifiers.control = control;
    modifiers.option = option;
    modifiers.command = command;
    modifiers.caps_lock = capsLock;
    modifiers.function = function;
    const astelio::mac::KeyResult result = _session->KeyDown(keyCode, modifiers);
    if (!result.handled) {
        return nil;
    }
    return [[ASTOutput alloc] initWithOutput:result.output];
}

- (ASTModeSwitch)modifierChangedWithKeyCode:(uint16_t)keyCode down:(BOOL)down timeMs:(uint64_t)timeMs
{
    const std::optional<bool> japanese = _session->ModifierChanged(keyCode, down, timeMs);
    if (!japanese) {
        return ASTModeSwitchNoChange;
    }
    return *japanese ? ASTModeSwitchJapanese : ASTModeSwitchEnglish;
}

- (void)markChordUsed
{
    _session->MarkChordUsed();
}

- (void)abandon
{
    _session->Abandon();
}

- (void)resetContext
{
    _session->ResetContext();
}

- (BOOL)composing
{
    return _session->Composing();
}

- (ASTComposition*)composition
{
    return [[ASTComposition alloc] initWithComposition:_session->CurrentComposition()];
}

- (ASTCandidateView*)candidates
{
    return [[ASTCandidateView alloc] initWithView:_session->Candidates()];
}

- (ASTOutput*)pick:(NSInteger)index
{
    if (index < 0) {
        return [[ASTOutput alloc] initWithOutput:astelio::SessionOutput{}];
    }
    return [[ASTOutput alloc] initWithOutput:_session->Pick(static_cast<std::size_t>(index))];
}

- (ASTOutput*)pickRelated:(NSInteger)index
{
    if (index < 0) {
        return [[ASTOutput alloc] initWithOutput:astelio::SessionOutput{}];
    }
    return [[ASTOutput alloc] initWithOutput:_session->PickRelated(static_cast<std::size_t>(index))];
}
@end
