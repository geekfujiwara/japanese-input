import Foundation
import SwiftUI

/// What the setup tab shows: whether the input method is installed and turned on, and the buttons that change it.
@MainActor
final class SetupModel: ObservableObject {
    @Published private(set) var status: InputMethodInstaller.Status = .notInstalled
    @Published var message: String?
    @Published private(set) var installedVersion: String?

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
        status = installer.status()
        installedVersion = installer.installedVersion
    }

    func install() {
        do {
            let outcome = try installer.install()
            if outcome.registerStatus != noErr {
                message = "入力ソースへの登録が失敗しました（エラー \(outcome.registerStatus)）。ログアウトして入り直してから、もう一度お試しください。"
            } else if !outcome.listed {
                message = "インストールしました。入力ソースの一覧に出るまで少し時間がかかります。出ないときは、一度ログアウトして入り直してください。"
            } else {
                message = nil
            }
        } catch {
            message = "インストールできませんでした: \(error.localizedDescription)"
        }
        refresh()
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
