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

    /// Stops the running server and removes an old copy, so a new one can be put in place.
    func prepareForInstall() throws {
        try FileManager.default.createDirectory(at: Self.folder, withIntermediateDirectories: true)
        terminateRunningServer()
        if FileManager.default.fileExists(atPath: Self.installedURL.path) {
            try FileManager.default.removeItem(at: Self.installedURL)
        }
    }

    /// Copies this app to `destination` (the place the user chose in the save panel).
    func copy(to destination: URL) throws {
        try FileManager.default.copyItem(at: Bundle.main.bundleURL, to: destination)
    }

    /// macOS refuses to run a program that a sandboxed app created on its own ("created without user consent"):
    /// the quarantine flags of such a file lack the user-approved bit (0x40). A copy the user made in the save panel
    /// or in the Finder has it, or has no quarantine at all.
    func canRun(_ url: URL) -> Bool {
        let name = "com.apple.quarantine"
        let size = getxattr(url.path, name, nil, 0, 0, XATTR_NOFOLLOW)
        guard size > 0 else {
            return true
        }
        var buffer = [UInt8](repeating: 0, count: size)
        guard getxattr(url.path, name, &buffer, size, 0, XATTR_NOFOLLOW) == size,
              let text = String(bytes: buffer, encoding: .utf8),
              let field = text.split(separator: ";").first,
              let flags = UInt32(field, radix: 16) else {
            return false
        }
        return flags & 0x40 != 0 || flags & 0x80 == 0
    }

    func removeInstalledCopy() {
        terminateRunningServer()
        try? FileManager.default.removeItem(at: Self.installedURL)
    }

    /// Tells the system about the copy in ~/Library/Input Methods and turns its input sources on.
    @discardableResult
    func register() -> Outcome {
        let status = TISRegisterInputSource(Self.installedURL as CFURL)
        var listed = !sources().isEmpty
        if !listed {
            // The list can lag behind the registration by a moment.
            Thread.sleep(forTimeInterval: 1)
            listed = !sources().isEmpty
        }
        for source in sources() {
            TISEnableInputSource(source)
        }
        return Outcome(registerStatus: status, listed: listed)
    }

    /// True when a copy is in place but the system does not list it yet (it was put there by hand).
    var needsRegistration: Bool {
        installedVersion != nil && sources().isEmpty
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
}
