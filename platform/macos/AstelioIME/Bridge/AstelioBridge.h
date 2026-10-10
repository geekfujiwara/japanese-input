// The Objective-C face of the C++ engine (platform/macos/engine) for the Swift input method. Only types cross here;
// the behavior lives in astelio::mac::MacSession, which is tested on every OS.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ASTSpanStyle) {
    ASTSpanStyleInput,
    ASTSpanStyleConverted,
    ASTSpanStyleFocused,
};

typedef NS_ENUM(NSInteger, ASTCandidateKind) {
    ASTCandidateKindHidden,
    ASTCandidateKindPredictions,
    ASTCandidateKindCandidates,
    ASTCandidateKindEmoji,
};

typedef NS_ENUM(NSInteger, ASTCandidateMark) {
    ASTCandidateMarkPlain,
    ASTCandidateMarkLearned,
    ASTCandidateMarkTypo,
};

typedef NS_ENUM(NSInteger, ASTTapModifier) {
    ASTTapModifierCommand,
    ASTTapModifierOption,
};

/// The result of a modifier change: no switch, or the mode to switch to.
typedef NS_ENUM(NSInteger, ASTModeSwitch) {
    ASTModeSwitchNoChange,
    ASTModeSwitchEnglish,
    ASTModeSwitchJapanese,
};

/// A run of the uncommitted text and how it is underlined (UTF-16 offsets).
@interface ASTSpan : NSObject
@property (nonatomic, readonly) NSRange range;
@property (nonatomic, readonly) ASTSpanStyle style;
@end

@interface ASTComposition : NSObject
@property (nonatomic, readonly, copy) NSString *text;
@property (nonatomic, readonly) NSInteger cursor;
@property (nonatomic, readonly, copy) NSArray<ASTSpan *> *spans;
/// Where the candidate window points (the focused segment).
@property (nonatomic, readonly) NSInteger anchor;
@end

@interface ASTCandidateView : NSObject
@property (nonatomic, readonly) ASTCandidateKind kind;
@property (nonatomic, readonly, copy) NSArray<NSString *> *items;
/// ASTCandidateMark values, one per item.
@property (nonatomic, readonly, copy) NSArray<NSNumber *> *marks;
/// Index into items, or -1.
@property (nonatomic, readonly) NSInteger selected;
@property (nonatomic, readonly) NSInteger firstIndex;
@property (nonatomic, readonly) NSInteger total;
@property (nonatomic, readonly) NSInteger columns;
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSArray<NSString *> *related;
/// Index into related, or -1.
@property (nonatomic, readonly) NSInteger relatedSelected;
@end

@interface ASTOutput : NSObject
@property (nonatomic, readonly, copy) NSString *commit;
/// B-08: remove this text just before the caret before showing the composition.
@property (nonatomic, readonly, copy) NSString *undoCommit;
@property (nonatomic, readonly) BOOL compositionChanged;
@end

@interface ASTSettings : NSObject
@property (nonatomic) ASTTapModifier tapModifier;
@property (nonatomic) BOOL learningEnabled;
@property (nonatomic) BOOL learningPaused;
@property (nonatomic) BOOL typoSuggestions;
@property (nonatomic) BOOL listNumberPeriod;
@property (nonatomic) BOOL autoCloseBrackets;
/// C-13: index into the date forms (0: yyyy/MM/dd ... 7: M/d).
@property (nonatomic) NSInteger dateFormat;
@end

/// The dictionary and the user's data, shared by every text field. Main thread only.
@interface ASTEngine : NSObject
- (instancetype)initWithDictionaryPath:(nullable NSString *)dictionaryPath
                         dataDirectory:(NSString *)dataDirectory
    NS_SWIFT_NAME(init(dictionaryPath:dataDirectory:)) NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;
@property (nonatomic, readonly) BOOL hasDictionary;
@property (nonatomic, readonly, copy) NSString *dataDirectory;
- (BOOL)clearLearning NS_SWIFT_NAME(clearLearning());
@end

/// One text field's input. Main thread only.
@interface ASTSession : NSObject
- (instancetype)initWithEngine:(ASTEngine *)engine NS_SWIFT_NAME(init(engine:)) NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

- (void)applySettings:(ASTSettings *)settings NS_SWIFT_NAME(applySettings(_:));
/// Secure Input or a password field: nothing is recorded.
- (void)setRecordingAllowed:(BOOL)allowed NS_SWIFT_NAME(setRecordingAllowed(_:));

@property (nonatomic, readonly) BOOL japaneseMode;
- (ASTOutput *)changeModeToJapanese:(BOOL)japanese NS_SWIFT_NAME(changeMode(toJapanese:));

/// nil when the key is not handled (the app gets it).
- (nullable ASTOutput *)keyDownWithKeyCode:(uint16_t)keyCode
                                     shift:(BOOL)shift
                                   control:(BOOL)control
                                    option:(BOOL)option
                                   command:(BOOL)command
                                  capsLock:(BOOL)capsLock
                                  function:(BOOL)function
    NS_SWIFT_NAME(keyDown(keyCode:shift:control:option:command:capsLock:function:));
- (ASTModeSwitch)modifierChangedWithKeyCode:(uint16_t)keyCode
                                       down:(BOOL)down
                                     timeMs:(uint64_t)timeMs NS_SWIFT_NAME(modifierChanged(keyCode:down:timeMs:));
- (void)markChordUsed NS_SWIFT_NAME(markChordUsed());

- (void)abandon NS_SWIFT_NAME(abandon());
- (void)resetContext NS_SWIFT_NAME(resetContext());

@property (nonatomic, readonly) BOOL composing;
- (ASTComposition *)composition NS_SWIFT_NAME(currentComposition());
- (ASTCandidateView *)candidates NS_SWIFT_NAME(currentCandidates());
/// Mouse: items[index] of candidates.
- (ASTOutput *)pick:(NSInteger)index NS_SWIFT_NAME(pick(_:));
- (ASTOutput *)pickRelated:(NSInteger)index NS_SWIFT_NAME(pickRelated(_:));
@end

NS_ASSUME_NONNULL_END
