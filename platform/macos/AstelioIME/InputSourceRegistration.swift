import Carbon
import Foundation

/// Registers this copy of the input method with Text Input Sources and turns it on, so it appears in the input menu
/// without a trip to System Settings (used by tools/install-astelio-mac.sh).
enum InputSourceRegistration {
    static func install() -> Bool {
        let url = Bundle.main.bundleURL as CFURL
        let status = TISRegisterInputSource(url)
        if status != noErr {
            print("TISRegisterInputSource failed: \(status)")
            return false
        }
        let sources = inputSources()
        if sources.isEmpty {
            print("Registered, but the input source is not listed yet. Log out and in, then add it in System Settings.")
            return true
        }
        for source in sources {
            let enabled = TISEnableInputSource(source)
            if enabled != noErr {
                print("TISEnableInputSource failed: \(enabled)")
            }
        }
        print("Astelio IME is on. Choose it from the input menu in the menu bar.")
        return true
    }

    /// Registers this copy without turning it on; harmless when it is already known.
    static func register() {
        let status = TISRegisterInputSource(Bundle.main.bundleURL as CFURL)
        if status != noErr {
            NSLog("Astelio IME: TISRegisterInputSource returned %ld", Int(status))
        }
    }

    static func uninstall() -> Bool {
        for source in inputSources() {
            TISDisableInputSource(source)
        }
        return true
    }

    private static func inputSources() -> [TISInputSource] {
        guard let bundleID = Bundle.main.bundleIdentifier else {
            return []
        }
        let filter = [kTISPropertyBundleID as String: bundleID] as CFDictionary
        guard let list = TISCreateInputSourceList(filter, true)?.takeRetainedValue() as? [TISInputSource] else {
            return []
        }
        return list
    }
}
