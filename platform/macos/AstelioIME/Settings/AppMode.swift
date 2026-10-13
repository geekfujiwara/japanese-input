import Foundation

enum AppMode {
    static let bundleID = "com.geekfujiwara.inputmethod.Astelio"
    static let clearLearningNotification = Notification.Name("com.geekfujiwara.inputmethod.Astelio.ClearLearning")
    /// Posted by the switcher for a single press of Shift: the input method leaves the English text started with Shift.
    static let shiftTapNotification = Notification.Name("com.geekfujiwara.inputmethod.Astelio.ShiftTap")

    /// True for the copy macOS starts as the input method (in an "Input Methods" folder), false for the settings app.
    static var isInputMethodServer: Bool {
        if CommandLine.arguments.contains("--input-method") {
            return true
        }
        return Bundle.main.bundleURL.deletingLastPathComponent().lastPathComponent == "Input Methods"
    }
}
