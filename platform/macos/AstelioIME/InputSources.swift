import Carbon
import Foundation

/// Choosing input sources: Astelio, or Apple's English input (R-03 on macOS: left Command switches to English,
/// right Command to Astelio, across input sources rather than inside Astelio).
enum InputSources {
    static func isAstelioCurrent() -> Bool {
        let current = TISCopyCurrentKeyboardInputSource().takeRetainedValue()
        return string(current, kTISPropertyBundleID) == AppMode.bundleID
    }

    @discardableResult
    static func selectAstelio() -> Bool {
        let filter = [kTISPropertyBundleID as String: AppMode.bundleID] as CFDictionary
        let sources = (TISCreateInputSourceList(filter, false)?.takeRetainedValue() as? [TISInputSource]) ?? []
        guard let source = sources.first(where: { bool($0, kTISPropertyInputSourceIsSelectCapable) }) else {
            return false
        }
        return TISSelectInputSource(source) == noErr
    }

    /// Apple's English input: an enabled English keyboard layout (ABC, U.S. first), else an enabled English mode of
    /// Apple's own input methods (Japanese Romaji's 英字), else ABC turned on.
    @discardableResult
    static func selectEnglish() -> Bool {
        let enabled = (TISCreateInputSourceList(nil, false)?.takeRetainedValue() as? [TISInputSource]) ?? []
        let usable = enabled.filter {
            bool($0, kTISPropertyInputSourceIsSelectCapable) && bool($0, kTISPropertyInputSourceIsASCIICapable)
                && string($0, kTISPropertyBundleID) != AppMode.bundleID
        }
        let layouts = usable.filter { string($0, kTISPropertyInputSourceType) == (kTISTypeKeyboardLayout as String) }
        let preferred = ["com.apple.keylayout.ABC", "com.apple.keylayout.US"]
        let modes = usable.filter {
            string($0, kTISPropertyInputSourceType) == (kTISTypeKeyboardInputMode as String)
                && (string($0, kTISPropertyInputSourceID) ?? "").hasPrefix("com.apple.")
        }
        let choice = layouts.first { preferred.contains(string($0, kTISPropertyInputSourceID) ?? "") }
            ?? layouts.first ?? modes.first
        if let choice {
            return TISSelectInputSource(choice) == noErr
        }
        let filter = [kTISPropertyInputSourceID as String: "com.apple.keylayout.ABC"] as CFDictionary
        guard let abc = (TISCreateInputSourceList(filter, true)?.takeRetainedValue() as? [TISInputSource])?.first else {
            return false
        }
        TISEnableInputSource(abc)
        // A source just turned on is not always ready to be chosen; try again a moment later.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
            TISSelectInputSource(abc)
        }
        return TISSelectInputSource(abc) == noErr
    }

    private static func string(_ source: TISInputSource, _ key: CFString) -> String? {
        guard let pointer = TISGetInputSourceProperty(source, key) else {
            return nil
        }
        return Unmanaged<CFString>.fromOpaque(pointer).takeUnretainedValue() as String
    }

    private static func bool(_ source: TISInputSource, _ key: CFString) -> Bool {
        guard let pointer = TISGetInputSourceProperty(source, key) else {
            return false
        }
        return CFBooleanGetValue(Unmanaged<CFBoolean>.fromOpaque(pointer).takeUnretainedValue())
    }
}
