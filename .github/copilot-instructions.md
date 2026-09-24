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
- 履歴の書き換え、ファイルの一括削除はユーザーの確認なしに行わない

## 秘匿情報

- 秘匿・重要な情報（APIキー、証明書のパスワード、Appleの認証情報など）はリポジトリ直下の `.env` に置く。`.env` はGitの管理対象外
- 新しい変数を使うときは、値を空にした名前だけを [.env.example](../.env.example) に追加する
- コード・ドキュメント・コミット・PR・ログに秘匿情報を書かない。コマンドの出力に含まれる場合は伏せる
- CIで必要な秘匿情報はGitHubのSecretsに登録してもらう（登録はユーザーが行う）

## ブランチとPR

- `main` に直接コミット・送信しない。作業ごとに `main` からブランチを作る（名前は `phase<番号>/<内容>`、不具合は `fix/<内容>`、文書だけは `docs/<内容>`）
- コミットは意味のまとまりごとに分ける。メッセージは `<種類>: <内容>`（種類は feat / fix / test / docs / ci / chore）
- 作業が終わったらブランチを送信し、`main` 向けのPRを作る。PRには対象のテストID、確認した内容、残った課題を書く
- CIの結果を確認し、失敗したら同じブランチで直す。マージはユーザーが行う（指示がある場合を除く）
- GitHub CLIは `& "$env:LOCALAPPDATA\Programs\gh\bin\gh.exe"` で実行する（PATHに入っていない）

## ビルドとテスト（現時点）

```powershell
dotnet test KotohaIME.slnx -c Release
./tools/Publish-Release.ps1 -RuntimeIdentifier win-arm64   # artifacts/AstelioIME-win-arm64.zip
```

- 起動中の `AstelioIME.exe` は配布フォルダーをロックする。発行の前に `Get-Process AstelioIME -ErrorAction SilentlyContinue | Stop-Process -Force` を実行する
- 同じプロセス内から `SendInput` した入力は低レベルキーフックに届かない。キー処理のテストはフックの判定を直接呼ぶ（`ProcessKeyboardMessageForTest`）

C++ Core（CMakeプリセット: `windows-arm64` / `windows-x64` / `windows-x86` / `macos-universal`）:

```powershell
cmake --preset windows-arm64
cmake --build --preset windows-arm64
ctest --preset windows-arm64
```

- このPCにはVisual C++とCMakeがまだ入っていない。C++の変更はPRのCI（`.github/workflows/ci.yml`）でビルドとテストを確認する
- 外部の依存は版とSHA-256を固定する。GitHub Actionsの利用もコミットのSHAで固定する
- TIP・macOS版のビルド手順は、該当フェーズでここに追記する
