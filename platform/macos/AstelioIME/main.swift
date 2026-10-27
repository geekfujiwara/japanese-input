import Cocoa
import InputMethodKit

// The app is the input method server. The Mac App Store version stays in /Applications; macOS lists it as an input
// source through a link the user puts in ~/Library/Input Methods (InputMethodInstaller), and launches it by its bundle
// ID. The settings window appears only when the user opens the app.
//
// `AstelioIME --install` (run by tools/install-astelio-mac.sh for the version outside the App Store) registers this
// copy with the system and turns it on in the input sources; `--uninstall` turns it off.
let arguments = CommandLine.arguments.dropFirst()
if arguments.contains("--install") {
    exit(InputSourceRegistration.install() ? 0 : 1)
}
if arguments.contains("--uninstall") {
    exit(InputSourceRegistration.uninstall() ? 0 : 1)
}

#if DEBUG
if let index = CommandLine.arguments.firstIndex(of: "--snapshot"), CommandLine.arguments.count > index + 2 {
    MainActor.assumeIsolated {
        SettingsSnapshot.run(tab: Int(CommandLine.arguments[index + 1]) ?? 0,
                             output: URL(fileURLWithPath: CommandLine.arguments[index + 2]),
                             ready: CommandLine.arguments.contains("ready"))
    }
}
#endif

if arguments.contains("--switcher") {
    // Started at login by the LaunchAgent: switches input sources with single presses of Command.
    let application = NSApplication.shared
    application.setActivationPolicy(.prohibited)
    let switcher = ModifierSwitcher()
    switcher.start()
    application.run()
} else {
    let connectionName = Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String
        ?? "72XRBY5B5K.com.geekfujiwara.inputmethod.Astelio.Connection"
    if let server = IMKServer(name: connectionName, bundleIdentifier: Bundle.main.bundleIdentifier) {
        AppState.shared.server = server
    } else {
        NSLog("Astelio IME: could not start the input method server (%@)", connectionName)
    }
    // The settings window asks for this when the history is deleted, so the running server forgets it too.
    DistributedNotificationCenter.default().addObserver(
        forName: AppMode.clearLearningNotification, object: nil, queue: .main
    ) { _ in
        _ = AppState.shared.engine.clearLearning()
    }
    // InputMethodKit does not pass modifier presses to the input method; the switcher sees them and says so.
    DistributedNotificationCenter.default().addObserver(
        forName: AppMode.shiftTapNotification, object: nil, queue: .main
    ) { _ in
        AppState.shared.activeSession?.exitTemporaryEnglish()
    }
    let application = NSApplication.shared
    let delegate = SettingsAppDelegate()
    application.delegate = delegate
    application.setActivationPolicy(.accessory)
    application.run()
}
