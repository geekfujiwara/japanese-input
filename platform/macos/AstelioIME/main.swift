import Cocoa
import InputMethodKit

// One app, two roles. Installed in an "Input Methods" folder it is the input method server that macOS starts when
// Astelio IME is chosen. Anywhere else (/Applications from the Mac App Store or TestFlight, a double click) it is the
// settings window, whose first job is to copy itself into ~/Library/Input Methods: macOS lists an input method only
// from there.
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
} else if AppMode.isInputMethodServer {
    let connectionName = Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String
        ?? "com.geekfujiwara.inputmethod.Astelio_Connection"
    guard let server = IMKServer(name: connectionName, bundleIdentifier: Bundle.main.bundleIdentifier) else {
        NSLog("Astelio IME: could not start the input method server (%@)", connectionName)
        exit(1)
    }
    AppState.shared.server = server
    // The settings window asks for this when the history is deleted, so the running server forgets it too.
    DistributedNotificationCenter.default().addObserver(
        forName: AppMode.clearLearningNotification, object: nil, queue: .main
    ) { _ in
        _ = AppState.shared.engine.clearLearning()
    }
    NSApplication.shared.run()
} else {
    let application = NSApplication.shared
    let delegate = SettingsAppDelegate()
    application.delegate = delegate
    application.setActivationPolicy(.regular)
    application.run()
}
