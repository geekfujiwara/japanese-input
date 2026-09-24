---
description: "Use when implementing the macOS input method: InputMethodKit controller, marked text, candidate window, left/right Command single-press switching, SwiftUI settings, Universal 2 build, notarization."
applyTo: "platform/macos/**"
---
# macOS版（InputMethodKit）

- Swiftで書き、C++ Coreとの橋渡しはObjective-C++に限る
- Universal 2（arm64 + x86_64）でビルドする
- 修飾キーの単押しは `flagsChanged` で判定する。既定は左Commandで英数、右Commandでかな（設定で変更可）
- Windows版と同じテストIDのケースを、模擬クライアント（`IMKTextInput`）を使ったXCTestで通す
- Secure Input中は日本語入力と学習を行わない
