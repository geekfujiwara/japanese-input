# Astelio IME 開発ガイド

英語配列キーボード向けの日本語入力システム（Windows ARM64 / x64、macOS）。作者 Geek Fujiwara、MITライセンス。

- 全体計画: [docs/astelio-ime-plan.md](../docs/astelio-ime-plan.md)（要件ID R / B / C / D、フェーズ0〜9）
- テスト計画: [docs/astelio-ime-test-plan.md](../docs/astelio-ime-test-plan.md)（テストID T-* / REG-* / P-* など）
- 現在の `src/`・`tests/` は旧キーフック版（.NET WPF）。フェーズ2でTSF版に置き換え、設定アプリとして残す

## 進め方

- 1回の作業（1セッション）は、計画書のフェーズ内の1項目か、テストIDの小さなまとまりに限る。完了したら新しいセッションで次へ進む
- 作業の最初に、対象のテストIDと完了条件を確認する。計画書は必要な節だけ読む
- 実装より先にテストを書く。不具合の修正は、先に再現するテストを書いて失敗を確認してから直す
- 同じ方法で2回失敗したら、3回目を試さずに原因を分析し、方針を変えるかユーザーに確認する
- 変更は依頼された範囲に限る。仕様を変えた場合は、計画書とテスト計画書の該当箇所も同じ作業で更新する
- 回答は日本語で、簡潔に書く

## 守ること

- ライセンス: 同梱する第三者のコード・データ・モデルはMIT・Apache-2.0・BSD系に限る。GPL系、独自の利用規約（Gemma、Llamaなど）、CC BY-SAは同梱しない。採用したものは `THIRD_PARTY_NOTICES` に記録する
- プライバシー: IMEと変換サーバーは外部と通信しない。入力内容をログに書かない。パスワード欄とシークレットモードでは学習しない
- 対象は英語配列のみ。日本語配列（JIS）固有のキーには対応しない
- モデルの使い分け: 開発はClaude Opus 5.5で行う。SLMは辞書作成環境の中でだけ使い、BYOKでローカル（Ollama / Foundry Local / llama.cpp）につなぐ。IME本体と辞書作成環境からクラウドのモデルを呼ばない
- 管理者権限が必要な操作（ツールの導入、TIPの登録）は実行せず、コマンドをユーザーに示す
- `git push`、履歴の書き換え、ファイルの一括削除はユーザーの確認なしに行わない

## ビルドとテスト（現時点）

```powershell
dotnet test KotohaIME.slnx -c Release
./tools/Publish-Release.ps1 -RuntimeIdentifier win-arm64   # artifacts/AstelioIME-win-arm64.zip
```

- 起動中の `AstelioIME.exe` は配布フォルダーをロックする。発行の前に `Get-Process AstelioIME -ErrorAction SilentlyContinue | Stop-Process -Force` を実行する
- 同じプロセス内から `SendInput` した入力は低レベルキーフックに届かない。キー処理のテストはフックの判定を直接呼ぶ（`ProcessKeyboardMessageForTest`）
- C++ Core・TIP・macOS版のビルド手順は、フェーズ0で環境を整えた時点でここに追記する
