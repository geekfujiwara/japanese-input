import Foundation

enum AppMode {
    static let bundleID = "com.geekfujiwara.inputmethod.Astelio"
    static let clearLearningNotification = Notification.Name("com.geekfujiwara.inputmethod.Astelio.ClearLearning")

    /// True for the copy macOS starts as the input method (in an "Input Methods" folder), false for the settings app.
    static var isInputMethodServer: Bool {
        if CommandLine.arguments.contains("--input-method") {
            return true
        }
        return Bundle.main.bundleURL.deletingLastPathComponent().lastPathComponent == "Input Methods"
    }
}
