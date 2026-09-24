# Astelio IME

Windows on ARMで動作する、MS-IME向けの入力カスタマイザーです。かな漢字変換エンジンは持たず、変換はWindows標準のMS-IMEに任せます。

開発: Geek Fujiwara / ライセンス: MIT

## 現在の機能

- 初回起動時の3ステップ設定ウィザード
- 数字・指定記号を入力モードにかかわらず半角で直接入力（既定: オン）
- 英字は半角の未確定文字としてMS-IMEへ渡し、Shiftによる大文字入力とスペースによる候補変換を利用
- `「 」 ！ ？ ・ 。`に相当するキーを半角で直接入力（既定: オフ）
- 左Altの単独押しでIMEをオフにして英語入力へ切り替え（既定: オン）
- 右Altの単独押しでIMEをオンにして日本語入力へ切り替え（既定: オン）
- Windowsサインイン時の自動起動
- タスクバーの通知領域に常駐
- 設定を`%LOCALAPPDATA%\AstelioIME\settings.json`へ自動保存

文字幅の差し替え時も、MS-IMEの現在の変換モードは変更しません。

初回起動時に入力スタイル、左右Altキー、自動起動を順番に設定できます。自動起動を有効にすると、現在の実行ファイルがユーザー単位のWindowsスタートアップへ登録されます。これらは後から通常の設定画面でも変更できます。

ウィンドウの閉じるボタンまたは「通知領域に隠す」で設定画面を閉じても、Astelio IMEは動作を続けます。通知領域アイコンのダブルクリックか「設定を開く」で画面を再表示し、「Astelio IMEを終了」で完全に終了します。

## 実行

```powershell
dotnet run --project src/KotohaIME.App/KotohaIME.App.csproj -r win-arm64
```

## ARM64向け発行

```powershell
.\tools\Publish-Release.ps1 -RuntimeIdentifier win-arm64
```

生成物は`artifacts/AstelioIME-win-arm64.zip`です。x64向けは`-RuntimeIdentifier win-x64`を指定します。

ZIPには自己完結型の`AstelioIME.exe`とMITライセンス本文の`LICENSE`が含まれます。EXEにはAstelio IMEのアプリアイコン、作者`Geek Fujiwara`、著作権情報が埋め込まれています。

## ライセンス

Copyright © 2026 Geek Fujiwara

このソフトウェアは[MIT License](LICENSE)で提供されます。

## 実装方式と制約

Astelio IME自体をWindowsのIMEとして登録するのではなく、低レベルキーボードフックで指定文字だけを直接入力し、かな漢字変換をMS-IMEへ委譲します。この方式なら独自辞書・変換エンジンを保守せず、ARM64ネイティブで配布できます。

WindowsのUIPI制約により、通常権限で起動したAstelio IMEは管理者権限のアプリへ入力を差し替えられません。また、セキュアデスクトップ、パスワード入力欄、一部のゲームや独自入力処理を持つアプリでは動作しない場合があります。これらまで一貫して対応する段階では、Text Services Frameworkによる署名付きTIPへの移行を検討します。