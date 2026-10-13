import Cocoa
import InputMethodKit

/// What every text field shares: the engine (dictionary and the user's data), the settings, the mode and the windows.
/// An input method serves all apps from one process, on the main thread.
final class AppState {
    static let shared = AppState()

    var server: IMKServer?
    let engine: ASTEngine
    let preferences = Preferences()
    lazy var candidatePanel = CandidatePanel()
    lazy var modeIndicator = ModeIndicator()

    /// C-14: the mode is shared by every app (as the input source itself is on macOS).
    var japaneseMode = true
    /// The session of the text field that has the input method now (for the Shift press seen by the switcher).
    weak var activeSession: ASTSession?

    private init() {
        let dictionary = Bundle.main.path(forResource: "system", ofType: "dic")
        engine = ASTEngine(dictionaryPath: dictionary, dataDirectory: AppState.dataDirectory().path)
        if !engine.hasDictionary {
            NSLog("Astelio IME: the system dictionary is missing or broken; only kana input works")
        }
    }

    /// ~/Library/Application Support/AstelioIME (inside the container when sandboxed).
    static func dataDirectory() -> URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Library/Application Support")
        return base.appendingPathComponent("AstelioIME", isDirectory: true)
    }
}
