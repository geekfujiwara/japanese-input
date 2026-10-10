import Foundation

/// The settings, kept in this input method's user defaults (com.geekfujiwara.inputmethod.Astelio).
final class Preferences {
    private enum Key {
        static let tapModifier = "TapModifier" // "command" or "option"
        static let learningEnabled = "LearningEnabled"
        static let learningPaused = "LearningPaused"
        static let typoSuggestions = "TypoSuggestions"
        static let listNumberPeriod = "ListNumberPeriod"
        static let autoCloseBrackets = "AutoCloseBrackets"
        static let dateFormat = "DateFormat"
    }

    private let defaults = UserDefaults.standard

    init() {
        defaults.register(defaults: [
            Key.tapModifier: "command",
            Key.learningEnabled: true,
            Key.learningPaused: false,
            Key.typoSuggestions: true,
            Key.listNumberPeriod: true,
            Key.autoCloseBrackets: true,
            Key.dateFormat: 0,
        ])
    }

    var tapModifier: ASTTapModifier {
        get { defaults.string(forKey: Key.tapModifier) == "option" ? .option : .command }
        set { defaults.set(newValue == .option ? "option" : "command", forKey: Key.tapModifier) }
    }

    var learningEnabled: Bool {
        get { defaults.bool(forKey: Key.learningEnabled) }
        set { defaults.set(newValue, forKey: Key.learningEnabled) }
    }

    var learningPaused: Bool {
        get { defaults.bool(forKey: Key.learningPaused) }
        set { defaults.set(newValue, forKey: Key.learningPaused) }
    }

    var typoSuggestions: Bool {
        get { defaults.bool(forKey: Key.typoSuggestions) }
        set { defaults.set(newValue, forKey: Key.typoSuggestions) }
    }

    var listNumberPeriod: Bool {
        get { defaults.bool(forKey: Key.listNumberPeriod) }
        set { defaults.set(newValue, forKey: Key.listNumberPeriod) }
    }

    var autoCloseBrackets: Bool {
        get { defaults.bool(forKey: Key.autoCloseBrackets) }
        set { defaults.set(newValue, forKey: Key.autoCloseBrackets) }
    }

    var dateFormat: Int {
        get { defaults.integer(forKey: Key.dateFormat) }
        set { defaults.set(newValue, forKey: Key.dateFormat) }
    }

    func settings() -> ASTSettings {
        let settings = ASTSettings()
        settings.tapModifier = tapModifier
        settings.learningEnabled = learningEnabled
        settings.learningPaused = learningPaused
        settings.typoSuggestions = typoSuggestions
        settings.listNumberPeriod = listNumberPeriod
        settings.autoCloseBrackets = autoCloseBrackets
        settings.dateFormat = dateFormat
        return settings
    }
}
