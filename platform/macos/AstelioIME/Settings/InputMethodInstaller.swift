import Carbon
import Cocoa

/// Puts the input method where macOS looks for it and reports whether it is turned on. The sandbox of the App Store
/// build may write to ~/Library/Input Methods through the entitlement in AstelioIME-AppStore.entitlements.
final class InputMethodInstaller {
    enum Status {
        case notInstalled
        case needsUpdate
        case needsEnabling
        case ready
    }

    struct Outcome {
        var registerStatus: OSStatus
        var listed: Bool
    }

    /// The real home folder: inside the sandbox NSHomeDirectory() is the container.
    static var realHome: URL {
        if let entry = getpwuid(getuid()), let directory = entry.pointee.pw_dir {
            return URL(fileURLWithPath: String(cString: directory), isDirectory: true)
        }
        return URL(fileURLWithPath: NSHomeDirectory(), isDirectory: true)
    }

    static var folder: URL {
        realHome.appendingPathComponent("Library/Input Methods", isDirectory: true)
    }

    static var installedURL: URL {
        folder.appendingPathComponent("AstelioIME.app", isDirectory: true)
    }

    static func versionText(of bundle: Bundle?) -> String? {
        guard let info = bundle?.infoDictionary else {
            return nil
        }
        let version = info["CFBundleShortVersionString"] as? String ?? "?"
        let build = info["CFBundleVersion"] as? String ?? "?"
        return "\(version) (\(build))"
    }

    var currentVersion: String { Self.versionText(of: Bundle.main) ?? "?" }
    var installedVersion: String? {
        FileManager.default.fileExists(atPath: Self.installedURL.path) ? Self.versionText(of: Bundle(url: Self.installedURL)) : nil
    }

    func status() -> Status {
        guard let installed = installedVersion else {
            return .notInstalled
        }
        if installed != currentVersion {
            return .needsUpdate
        }
        return isEnabled() ? .ready : .needsEnabling
    }

    // MARK: - Install and remove

    @discardableResult
    func install() throws -> Outcome {
        let fileManager = FileManager.default
        try fileManager.createDirectory(at: Self.folder, withIntermediateDirectories: true)
        terminateRunningServer()
        if fileManager.fileExists(atPath: Self.installedURL.path) {
            try fileManager.removeItem(at: Self.installedURL)
        }
        try fileManager.copyItem(at: Bundle.main.bundleURL, to: Self.installedURL)
        removeQuarantine(Self.installedURL)

        let status = TISRegisterInputSource(Self.installedURL as CFURL)
        var listed = !sources().isEmpty
        if listed {
            for source in sources() {
                TISEnableInputSource(source)
            }
        } else {
            // The list can lag behind the registration by a moment.
            Thread.sleep(forTimeInterval: 1)
            listed = !sources().isEmpty
        }
        return Outcome(registerStatus: status, listed: listed)
    }

    func uninstall() throws {
        for source in sources() {
            TISDisableInputSource(source)
        }
        terminateRunningServer()
        if FileManager.default.fileExists(atPath: Self.installedURL.path) {
            try FileManager.default.removeItem(at: Self.installedURL)
        }
    }

    // MARK: - Input sources

    func isEnabled() -> Bool {
        sources().contains { source in
            guard let pointer = TISGetInputSourceProperty(source, kTISPropertyInputSourceIsEnabled) else {
                return false
            }
            return CFBooleanGetValue(Unmanaged<CFBoolean>.fromOpaque(pointer).takeUnretainedValue())
        }
    }

    private func sources() -> [TISInputSource] {
        let filter = [kTISPropertyBundleID as String: AppMode.bundleID] as CFDictionary
        return (TISCreateInputSourceList(filter, true)?.takeRetainedValue() as? [TISInputSource]) ?? []
    }

    // MARK: - Helpers

    /// The server keeps running after a new copy is put in place; stop it so macOS starts the new one.
    private func terminateRunningServer() {
        let installed = Self.installedURL.standardizedFileURL
        for app in NSRunningApplication.runningApplications(withBundleIdentifier: AppMode.bundleID)
        where app.bundleURL?.standardizedFileURL == installed {
            if !app.terminate() {
                app.forceTerminate()
            }
        }
    }

    /// A copy made by a sandboxed app is marked as downloaded; the mark would make macOS ask before it loads.
    private func removeQuarantine(_ url: URL) {
        guard let items = FileManager.default.enumerator(atPath: url.path) else {
            return
        }
        removexattr(url.path, "com.apple.quarantine", XATTR_NOFOLLOW)
        for case let item as String in items {
            removexattr(url.appendingPathComponent(item).path, "com.apple.quarantine", XATTR_NOFOLLOW)
        }
    }
}
