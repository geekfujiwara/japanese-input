# macOS版（2026-10-10時点）

macOS版は InputMethodKit の入力メソッド（`AstelioIME.app`）です。変換エンジンは Windows 版と同じ C++ Core を使い、キー操作もそろえています（左右 Alt の代わりに左右 Command の単押し）。

## できること

- ローマ字入力、Space で変換、Return で確定、予測（Tab / ↓）、文節の移動と伸縮、F6〜F10、数字・日付、入力ミスの補正、もしかして、絵文字パレット、確定の取り消し（Control+Backspace）
- 左 Command の単押しで英語、右 Command の単押しで日本語（入力メニューで Option に変えられる）。切り替えると入力位置の近くに「あ」「A」を少し表示。モードはすべてのアプリで共有
- すりガラスの候補ウィンドウ（クリックでも選べる）。「もしかして」「履歴」の印
- 入力の履歴（学習）とその一時停止・全削除、ユーザー辞書（`user_dictionary.tsv`。Windows 版と同じ形式）
- パスワード欄（Secure Input）では日本語入力にならず、記録もしない
- 設定はメニューバーの入力メニュー（Astelio IME を選んでいるときの「あ」のアイコン）から変える

データは `~/Library/Application Support/AstelioIME/`（`learning.tsv`、`user_dictionary.tsv`、`emoji_recent.txt`）に、この利用者だけが読める形で置きます。外部とは通信しません。

## このMacで使う

必要なもの: macOS 14 以降、GitHub CLI（`brew install gh` のあと `gh auth login`）。

```bash
git clone https://github.com/geekfujiwara/japanese-input.git
cd japanese-input
tools/get-astelio-mac.sh            # main の最新のビルドを取得して入れる（ブランチ名も指定できる）
```

自分のMacでビルドするとき（Xcode と `brew install cmake xcodegen` が必要。初回は辞書の作成に数分かかる）:

```bash
tools/build-astelio-mac.sh --test   # build/macos-app/AstelioIME.app と zip を作る
tools/install-astelio-mac.sh        # ~/Library/Input Methods に入れて入力ソースに加える
```

入れたあと、メニューバーの入力メニューで「Astelio IME」を選びます。一覧にないときは［システム設定］→［キーボード］→［テキスト入力］の［編集...］→［+］→［日本語］から「Astelio IME」を加えます（出てこなければ一度ログアウトして入り直す）。取り除くときは `tools/install-astelio-mac.sh --uninstall` です。

> [!NOTE]
> 今のビルドは開発者の署名（Developer ID）と公証をしていない、アドホック署名です。スクリプトで入れる場合はそのまま動きます。ブラウザーで zip をダウンロードした場合は、スクリプトに zip を渡すと隔離の属性を外して入れます。

## ソースの構成

| 場所 | 内容 |
| --- | --- |
| `platform/macos/engine/` | OS に依存しない C++（キーコードの変換、辞書のメモリマップ、履歴などの保存、修飾キーの単押し、表示する内容）。Linux・macOS の CI で gtest を実行 |
| `platform/macos/AstelioIME/Bridge/` | Objective-C++ の橋渡し（型の変換だけ） |
| `platform/macos/AstelioIME/*.swift` | IMKInputController、候補ウィンドウ、モード表示、設定、入力ソースの登録 |
| `platform/macos/project.yml` | XcodeGen のプロジェクト定義（`.xcodeproj` はコミットしない） |
| `platform/macos/Tests/` | 橋渡しの XCTest |
| `.github/workflows/macos.yml` | Universal 2 のビルドとテスト（署名は ad hoc）。成果物 `astelio-ime-macos` に zip を置く |
| `tools/asc_mac_release.py`、`tools/release-astelio-mac-appstore.sh` | Mac App Store 用の署名・アップロード・TestFlight（開発者の Mac で実行する） |

## Mac App Store に出すには（まず TestFlight）

App Store 版は App Sandbox を有効にした `AstelioIME-AppStore.entitlements` で署名し、`/Applications` に入ります。ただし macOS は、`~/Library/Input Methods` か `/Library/Input Methods` にあるアプリしか入力ソースとして認識しません（`/Applications` のまま `TISRegisterInputSource` を呼んでも、成功を返すだけで一覧に出ないことを実機で確認）。そこで同じアプリが 2 つの役割を持ちます。

- `/Applications` などから開くと **設定アプリ**（SwiftUI）。［はじめに］の［インストール］で自分自身を `~/Library/Input Methods` にコピーして登録し、［システム設定を開く］から日本語の入力ソースに追加する。入力・履歴・ユーザー辞書の設定もここで行う
- `Input Methods` フォルダーのコピーは、macOS が起動する **入力メソッド本体**（画面なし）

置き場所で役割を決めます（`AppMode.isInputMethodServer`）。設定は同じ Bundle ID の UserDefaults とデータフォルダーで共有します。サンドボックスで `~/Library/Input Methods` に書くため、`temporary-exception.files.home-relative-path.read-write`（`/Library/Input Methods/`）の例外を使います。

署名とアップロードは、**開発者の Mac で** `tools/release-astelio-mac-appstore.sh` を実行して行います（このリポジトリは公開なので、App Store Connect の鍵を GitHub に置かず、署名のワークフローも置きません）。証明書（Apple Distribution と Mac Installer Distribution）とプロビジョニングプロファイルはその実行のためだけに App Store Connect API で作り、処理が終わったら失効させます。Apple が配布用に署名し直すので、失効させてもアップロード済みのビルドには影響しません。

準備（初回だけ）:

1. App Store Connect →［ユーザーとアクセス］→［統合］→［App Store Connect API］で、**Admin** の役割のキーを作り、`.p8` を Mac の `~/.appstoreconnect/private_keys/` に置く（再ダウンロードできない。リポジトリやチャットに貼らない）
2. `ASC_KEY_ID`・`ASC_ISSUER_ID`・`ASC_KEY_PATH` を環境変数に設定し、`python3 tools/asc_mac_release.py check --bundle-id com.geekfujiwara.inputmethod.Astelio` を実行する。Bundle ID（macOS）を登録し、アプリがまだ無いので止まる
3. App Store Connect →［アプリ］→［+］→［新規アプリ］で、プラットフォーム macOS、上の Bundle ID、名前「Astelio IME」、主言語 日本語、SKU（任意）でアプリを作る（API では作れない）
4. `tools/release-astelio-mac-appstore.sh --build-number <前回より大きい番号> --no-upload` で、署名した .pkg ができるところまで確かめる
5. `--no-upload` を外し、`--tester <自分の App Store Connect のメールアドレス>` を付けて実行する。処理が終わると Mac の TestFlight アプリに出る

内部テスト（チームのメンバー）は Beta 版審査が要りません。外部のテスターに配るときと、App Store に出すときは審査があります。

> [!WARNING]
> 入力メソッドが Mac App Store の審査を通るかは確かではありません（計画書4.7で「フェーズ7の始めに確かめ、難しければ.pkgを先に出す」としていた点）。
> - App Store のアプリは `/Applications` に入りますが、入力メソッドは本来 `~/Library/Input Methods` か `/Library/Input Methods` に置くものです。App Store 版は `/Applications` から入力ソースとして登録できるかを、実機で確かめる必要があります（`AstelioIME --install` は `TISRegisterInputSource` でその場所を登録します）
> - サンドボックスで入力メソッドを動かすには、一時的な例外のエンタイトルメント（`temporary-exception.mach-register.global-name`）が要ります。審査で理由を求められることがあります
> - 画面を持たないバックグラウンドのアプリは却下された例があります。App Store 版には、入れ方と設定を案内する通常のアプリ（SwiftUI の設定アプリ、フェーズ7の残り）を表に出し、入力メソッドはその中に入れる形が必要になる見込みです
>
> 先に Developer ID 署名と公証をした .pkg を GitHub Releases に出し（計画書4.7の配布の1つめ）、App Store は設定アプリができてから審査に相談するのが確実です。

## まだできていないこと

| 内容 | メモ |
| --- | --- |
| 実機での確認 | テキストエディット、Safari、VS Code で Windows 版と同じ操作ができること（フェーズ7の完了条件）。候補ウィンドウの位置、ライト/ダーク、マルチディスプレイ、Command の単押しとショートカットの区別 |
| 設定アプリの残り | アプリごとの設定、辞書の取り込み・書き出し（Windows 版にある他 IME の形式）、App Store 版のサンドボックス内での動作確認（TestFlight） |
| Developer ID 署名・公証・.pkg | 証明書と公証の認証情報を GitHub の Secrets に入れ、main とタグのビルドだけで署名する（計画書4.7） |
| Release への追加 | タグのビルドで zip（のちに .pkg）を GitHub Release に置く |
| 入力モードのアイコン | メニューバーのアイコンは Astelio のアイコン1つ。日本語・英語でアイコンを変えるには入力モード（`ComponentInputModeDict`）を使う |
| 再変換、アプリごとの無効化 | Windows 版の B-07、C-09 に当たるもの |
