import Carbon
import Cocoa

/// Makes macOS list this app as an input source and reports whether it is turned on.
///
/// macOS lists input methods only from an "Input Methods" folder, while a Mac App Store app stays in /Applications.
/// The app does not copy itself or any other code: the user puts a symbolic link to this app in
/// ~/Library/Input Methods through the save panel (the App Sandbox's user-selected access), and the input method keeps
/// running from the app bundle in /Applications.
final class InputMethodInstaller {
    enum Status {
        case notInstalled
        /// Something else is where the link goes: a copy of the app from an older version, or a link to another copy.
        case needsRepair
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

    static var linkURL: URL {
        folder.appendingPathComponent("AstelioIME.app", isDirectory: false)
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

    /// Where the link points, or nil when there is no link (nothing, or a real folder).
    static func linkTarget(at url: URL) -> URL? {
        guard let path = try? FileManager.default.destinationOfSymbolicLink(atPath: url.path) else {
            return nil
        }
        return URL(fileURLWithPath: path, relativeTo: url.deletingLastPathComponent()).standardizedFileURL
    }

    /// The version outside the App Store (Developer ID): the installer puts the app itself in /Library/Input Methods,
    /// so no link is needed and the app is listed from where it runs.
    static var runsFromInputMethods: Bool {
        Bundle.main.bundleURL.deletingLastPathComponent().lastPathComponent == "Input Methods"
    }

    /// What is registered with Text Input Sources: the app itself, or the link to it.
    static var registrationURL: URL {
        runsFromInputMethods ? Bundle.main.bundleURL : linkURL
    }

    var hasLink: Bool {
        Self.linkTarget(at: Self.linkURL) == Bundle.main.bundleURL.standardizedFileURL
    }

    /// Anything at the link's place (a link, a copy of the app, or a broken link).
    var somethingInPlace: Bool {
        (try? FileManager.default.attributesOfItem(atPath: Self.linkURL.path)) != nil
    }

    func status() -> Status {
        if hasLink || Self.runsFromInputMethods {
            return isEnabled() ? .ready : .needsEnabling
        }
        return somethingInPlace ? .needsRepair : .notInstalled
    }

    // MARK: - Link and register

    /// Puts a link to this app at `destination`, the place the user chose in the save panel (the panel asked before
    /// replacing what was there). `destination` may also be a new "Input Methods" folder the user chose to create.
    func makeLink(at destination: URL) throws {
        let fileManager = FileManager.default
        var link = destination
        if destination.lastPathComponent == "Input Methods" {
            try fileManager.createDirectory(at: destination, withIntermediateDirectories: false)
            link = destination.appendingPathComponent(Self.linkURL.lastPathComponent)
        } else if (try? fileManager.attributesOfItem(atPath: destination.path)) != nil {
            try fileManager.removeItem(at: destination)
        }
        try fileManager.createSymbolicLink(at: link, withDestinationURL: Bundle.main.bundleURL)
    }

    /// Tells the system about the link in ~/Library/Input Methods. Recent macOS asks the user to add the input source
    /// in System Settings; TISEnableInputSource is tried as well, for the systems that still allow it.
    @discardableResult
    func register() -> Outcome {
        let status = TISRegisterInputSource(Self.registrationURL as CFURL)
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

    /// True when the link is in place but the system does not list the input source yet.
    var needsRegistration: Bool {
        (hasLink || Self.runsFromInputMethods) && sources().isEmpty
    }

    /// Turns the input sources off. The link itself is removed by the user in the Finder (the app cannot write there
    /// on its own); a link left behind does nothing once the app is gone.
    func disable() {
        for source in sources() {
            TISDisableInputSource(source)
        }
    }

    /// Shows the link (or the old copy) in the Finder, so the user can move it to the Trash.
    func revealInFinder() {
        NSWorkspace.shared.activateFileViewerSelecting([Self.runsFromInputMethods ? Bundle.main.bundleURL : Self.linkURL])
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
}
