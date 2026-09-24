---
description: "Use when implementing the Windows TSF text input processor (TIP): COM registration, ITfKeyEventSink, composition, candidate window, Alt single-press switching, ARM64/x64/x86 builds, TSF host tests."
applyTo: "platform/windows/tip/**"
---
# Windows TIP（TSF）

- 低レベルキーボードフック（`WH_KEYBOARD_LL`）は使わない。キーは `ITfKeyEventSink`、Alt単押しは `ITfKeystrokeMgr::PreserveKey` とキーイベントで扱う
- TIPは入力先アプリのプロセス内で動く。COMの境界ですべての例外を捕まえて `HRESULT` を返し、入力先アプリを落とさない
- TIPは薄い層にする。変換・辞書・学習は変換サーバー（フェーズ4以降）に任せ、TIP内で重い処理をしない
- ARM64・x64・x86の3種類をビルドする。ARM64版Windows上でも、x64アプリと32bitアプリはそれぞれのDLLを読み込む
- 結合テストはテスト用ホスト（独自の `ITextStoreACP`）で行い、キーは `ITfKeystrokeMgr::TestKeyDown` / `KeyDown` / `KeyUp` で渡す。メニュー起動は `SC_KEYMENU` の受信回数で判定する
- 再発防止テスト（REG-01〜REG-07）は常に通ること
- TIPの登録・解除は管理者権限が必要なため、スクリプトを用意してユーザーに実行してもらう
