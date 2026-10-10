import Cocoa
import InputMethodKit

// `AstelioIME --install` (run by tools/install-astelio-mac.sh) registers this copy with the system and turns it on
// in the input sources; `--uninstall` turns it off. Without arguments the app is the input method server that macOS
// starts when Astelio IME is chosen.
let arguments = CommandLine.arguments.dropFirst()
if arguments.contains("--install") {
    exit(InputSourceRegistration.install() ? 0 : 1)
}
if arguments.contains("--uninstall") {
    exit(InputSourceRegistration.uninstall() ? 0 : 1)
}

let connectionName = Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String
    ?? "com.geekfujiwara.inputmethod.Astelio_Connection"
guard let server = IMKServer(name: connectionName, bundleIdentifier: Bundle.main.bundleIdentifier) else {
    NSLog("Astelio IME: could not start the input method server (%@)", connectionName)
    exit(1)
}
AppState.shared.server = server

let application = NSApplication.shared
application.run()
