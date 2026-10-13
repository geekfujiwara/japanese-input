import Cocoa
import InputMethodKit

// Every copy is the input method server: macOS finds the input method by its bundle ID, which Launch Services may
// resolve to the copy in /Applications (the Mac App Store one) as well as to the one in ~/Library/Input Methods (which
// macOS needs to list the input source at all). The settings window appears only when the user opens the app.
//
// `AstelioIME --install` (run by tools/install-astelio-mac.sh) registers this copy with the system and turns it on
// in the input sources; `--uninstall` turns it off.
let arguments = CommandLine.arguments.dropFirst()
if arguments.contains("--install") {
    exit(InputSourceRegistration.install() ? 0 : 1)
}
if arguments.contains("--uninstall") {
    exit(InputSourceRegistration.uninstall() ? 0 : 1)
}

// For scripts and tests: copy to ~/Library/Input Methods and register (a copy made here, outside the sandbox, runs).
if arguments.contains("--install-input-method") {
    do {
        let installer = InputMethodInstaller()
        try installer.prepareForInstall()
        try installer.copy(to: InputMethodInstaller.installedURL)
        let outcome = installer.register()
        print("registered: \(outcome.registerStatus), listed: \(outcome.listed)")
        exit(outcome.registerStatus == noErr ? 0 : 1)
    } catch {
        print("install failed: \(error)")
        exit(1)
    }
}

if arguments.contains("--switcher") {
    // Started at login by the LaunchAgent: switches input sources with single presses of Command.
    let application = NSApplication.shared
    application.setActivationPolicy(.prohibited)
    let switcher = ModifierSwitcher()
    switcher.start()
    application.run()
} else {
    let connectionName = Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String
        ?? "com.geekfujiwara.inputmethod.Astelio_Connection"
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
