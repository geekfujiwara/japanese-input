import AppKit
import CoreGraphics
import Foundation
import ServiceManagement
import SwiftUI

/// What the setup tab shows: whether the input method is installed and turned on, and the buttons that change it.
@MainActor
final class SetupModel: ObservableObject {
    @Published private(set) var status: InputMethodInstaller.Status = .notInstalled
    @Published var message: String?
    /// R-03 across input sources: `AstelioIME --switcher` at login and the Input Monitoring permission.
    @Published private(set) var switcherRegistered = false
    @Published private(set) var switcherNeedsApproval = false
    @Published private(set) var inputMonitoringAllowed = false
    private let switcher = SMAppService.agent(plistName: "com.geekfujiwara.inputmethod.Astelio.switcher.plist")

    let currentVersion: String
    #if DEBUG
    /// For SettingsSnapshot: the status to show instead of this Mac's.
    static var shownStatus: InputMethodInstaller.Status?
    #endif
    private let installer = InputMethodInstaller()
    private var timer: Timer?

    init() {
        currentVersion = installer.currentVersion
        refresh()
        // The user adds the input source in System Settings, in another window; notice it without a click.
        timer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refresh() }
        }
    }

    deinit {
        timer?.invalidate()
    }

    func refresh() {
        // A link from an earlier run, or one the system forgot, is registered again.
        if installer.needsRegistration {
            installer.register()
        }
        status = installer.status()
        #if DEBUG
        if let shown = Self.shownStatus {
            status = shown
        }
        #endif
        switcherRegistered = switcher.status == .enabled
        switcherNeedsApproval = switcher.status == .requiresApproval
        inputMonitoringAllowed = CGPreflightListenEventAccess()
    }

    /// Asks for Input Monitoring (macOS shows its dialog once) and starts the switcher now and at every login.
    func enableSwitcher() {
        if !CGPreflightListenEventAccess() {
            _ = CGRequestListenEventAccess()
        }
        do {
            try switcher.register()
        } catch {
            if switcher.status != .requiresApproval {
                message = "左右⌘の切り替えを始められませんでした: \(error.localizedDescription)"
            }
        }
        if switcher.status == .requiresApproval {
            SMAppService.openSystemSettingsLoginItems()
        }
        refresh()
    }

    func disableSwitcher() {
        try? switcher.unregister()
        refresh()
    }

    func openInputMonitoringSettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent") {
            NSWorkspace.shared.open(url)
        }
    }

    /// The save panel is the user's choice of where the link goes: the App Sandbox lets the app write only there.
    /// Nothing is copied; the link points to this app, which stays where the App Store put it.
    func install() {
        message = nil
        let fileManager = FileManager.default
        let folder = InputMethodInstaller.folder
        let folderExists = fileManager.fileExists(atPath: folder.path)
        let panel = NSSavePanel()
        panel.canCreateDirectories = true
        panel.showsHiddenFiles = false
        panel.prompt = "追加"
        panel.title = "Astelio IME を入力ソースに追加"
        if folderExists {
            panel.directoryURL = folder
            panel.nameFieldStringValue = InputMethodInstaller.linkURL.lastPathComponent
            panel.message = "「Input Methods」フォルダーに Astelio へのリンクを置きます（アプリはコピーしません）。このまま［追加］を押してください。"
        } else {
            // A new user has no Input Methods folder yet: the user creates it here, and the link goes inside.
            panel.directoryURL = folder.deletingLastPathComponent()
            panel.nameFieldStringValue = folder.lastPathComponent
            panel.message = "「ライブラリ」に「Input Methods」フォルダーを作り、その中に Astelio へのリンクを置きます（アプリはコピーしません）。このまま［追加］を押してください。"
        }
        NSApp.activate(ignoringOtherApps: true)
        guard panel.runModal() == .OK, let destination = panel.url else {
            refresh()
            return
        }
        let expected = folderExists ? InputMethodInstaller.linkURL : folder
        guard destination.standardizedFileURL.path == expected.standardizedFileURL.path else {
            message = "場所や名前を変えずに［追加］を押してください（\(expected.path)）。"
            refresh()
            return
        }
        do {
            try installer.makeLink(at: destination)
        } catch {
            message = "リンクを作れませんでした: \(error.localizedDescription)"
            refresh()
            return
        }
        let outcome = installer.register()
        if outcome.registerStatus != noErr {
            message = "入力ソースへの登録が失敗しました（エラー \(outcome.registerStatus)）。ログアウトして入り直してから、もう一度お試しください。"
        } else if !outcome.listed {
            message = "追加しました。入力ソースの一覧に出るまで少し時間がかかります。出ないときは、一度ログアウトして入り直してください。"
        }
        refresh()
        if status == .needsEnabling {
            openKeyboardSettings()
        }
    }

    /// Registers the input source and asks macOS to turn it on; when macOS leaves that to the user, opens the
    /// keyboard settings where the user adds it.
    func turnOn() {
        message = nil
        let outcome = installer.register()
        refresh()
        if status != .ready {
            if outcome.registerStatus != noErr {
                message = "入力ソースへの登録が失敗しました（エラー \(outcome.registerStatus)）。"
            }
            openKeyboardSettings()
        }
    }

    /// Shows the link (or a copy of the app left by an older version) in the Finder, to be moved to the Trash.
    func showInFinder() {
        installer.revealInFinder()
    }

    func uninstall() {
        installer.disable()
        message = "入力ソースから外しました。Finder で選ばれている「AstelioIME.app」（Input Methods フォルダー内のリンク）をゴミ箱に入れると、取り除けます。履歴とユーザー辞書は残っています。"
        installer.revealInFinder()
        refresh()
    }

    func openKeyboardSettings() {
        let urls = [
            "x-apple.systempreferences:com.apple.Keyboard-Settings.extension",
            "x-apple.systempreferences:com.apple.preference.keyboard",
        ]
        for text in urls {
            if let url = URL(string: text), NSWorkspace.shared.open(url) {
                return
            }
        }
    }
}
