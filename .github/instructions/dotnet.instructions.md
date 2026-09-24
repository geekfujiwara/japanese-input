---
description: "Use when editing the .NET code: legacy key-hook app, WPF settings app, tray icon, setup wizard, settings migration, xUnit tests, dictionary tooling written in C#."
applyTo: ["src/**", "tests/**", "platform/windows/settings/**", "dictionary/tools/**/*.cs"]
---
# .NET（設定アプリ・旧版・ツール）

- .NET 10。アプリは `net10.0-windows`、OSに依存しないライブラリとテストは `net10.0`
- 名前空間とプロジェクト名は現在 `KotohaIME.*`。`Astelio.*` への改名は移設（`platform/windows/settings/`）のときにまとめて行い、それまでは混在させない
- テストはxUnit。アプリ内部の確認には `InternalsVisibleTo` を使い、テストのためだけに公開APIを増やさない
- 設定ファイル（`%LOCALAPPDATA%\AstelioIME\settings.json`）の形式を変えるときは、旧形式からの移行処理とテスト（T-C10-2）を同時に書く
- P/Invokeの構造体はWin32の定義と同じ並び・型にし、ARM64とx64の両方でビルドを確認する
