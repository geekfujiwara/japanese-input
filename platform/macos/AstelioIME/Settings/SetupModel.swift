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
    @Published private(set) var installedVersion: String?
    @Published var manualInstall = false
    /// R-03 across input sources: `AstelioIME --switcher` at login and the Input Monitoring permission.
    @Published private(set) var switcherRegistered = false
    @Published private(set) var switcherNeedsApproval = false
    @Published private(set) var inputMonitoringAllowed = false
    private let switcher = SMAppService.agent(plistName: "com.geekfujiwara.inputmethod.Astelio.switcher.plist")

    let currentVersion: String
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
        // A copy put there by hand (Finder) is not known to the system yet.
        if installer.needsRegistration {
            installer.register()
            manualInstall = false
        }
        status = installer.status()
        installedVersion = installer.installedVersion
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

    /// The save panel is the user's consent for the copy: macOS will not run a program a sandboxed app wrote by itself.
    func install() {
        message = nil
        do {
            try installer.prepareForInstall()
        } catch {
            message = "古いコピーを取り除けませんでした: \(error.localizedDescription)"
            refresh()
            return
        }
        let panel = NSSavePanel()
        panel.directoryURL = InputMethodInstaller.folder
        panel.nameFieldStringValue = InputMethodInstaller.installedURL.lastPathComponent
        panel.canCreateDirectories = false
        panel.prompt = "インストール"
        panel.title = "Astelio IME をインストール"
        panel.message = "「Input Methods」フォルダーのまま［インストール］を押してください。"
        guard panel.runModal() == .OK, let destination = panel.url else {
            refresh()
            return
        }
        guard destination.deletingLastPathComponent().lastPathComponent == "Input Methods" else {
            message = "保存先は「Input Methods」フォルダーにしてください（ライブラリ内の Input Methods）。"
            refresh()
            return
        }
        do {
            try installer.copy(to: destination)
        } catch {
            message = "コピーできませんでした: \(error.localizedDescription)"
            manualInstall = true
            refresh()
            return
        }
        guard installer.canRun(destination) else {
            // The system would refuse to start this copy; the Finder's copy is fine.
            installer.removeInstalledCopy()
            manualInstall = true
            message = "この Mac では、アプリが自分でコピーしたものは起動できませんでした。下の手順で、Finder からコピーしてください。"
            refresh()
            return
        }
        let outcome = installer.register()
        if outcome.registerStatus != noErr {
            message = "入力ソースへの登録が失敗しました（エラー \(outcome.registerStatus)）。ログアウトして入り直してから、もう一度お試しください。"
        } else if !outcome.listed {
            message = "インストールしました。入力ソースの一覧に出るまで少し時間がかかります。出ないときは、一度ログアウトして入り直してください。"
        }
        refresh()
    }

    /// Opens the Input Methods folder and shows this app, so the user can drag it across (the Finder's copy runs).
    func showInFinder() {
        manualInstall = true
        try? FileManager.default.createDirectory(at: InputMethodInstaller.folder, withIntermediateDirectories: true)
        NSWorkspace.shared.open(InputMethodInstaller.folder)
        NSWorkspace.shared.activateFileViewerSelecting([Bundle.main.bundleURL])
    }

    func uninstall() {
        do {
            try installer.uninstall()
            message = "取り除きました。履歴とユーザー辞書は残っています。"
        } catch {
            message = "取り除けませんでした: \(error.localizedDescription)"
        }
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
