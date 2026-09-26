---
description: "ユーザー辞書（Windows TIP）: 保存・読み直し、管理画面、取り込み・書き出しの画面（D-02・D-03）"
name: "astelio-user-dictionary-tip"
agent: "agent"
model: "Claude Opus 5.5 (copilot)"
---
ユーザー辞書のWindows TIP部分を1回分の作業として実装してください。この作業は、ほかの2つの作業（`/astelio-user-dictionary-core`、`/astelio-user-dictionary-io`）と並行して進みます。マージはこの作業が最後です。

## 対象

- 要件: D-02（管理画面での追加・編集・削除・検索）、D-03（画面からの取り込み・書き出し）
- テストID: T-D02-2、T-D03-3（[テスト計画書](../../docs/astelio-ime-test-plan.md)）
- ブランチ: `phase5/user-dictionary-tip`（最新の `main` から作る）
- 計画書で読む節: 2章のD-02・D-03、4.6（データの保存）、12章。既存の入力履歴の実装（`platform/windows/tip/src/learning_store.*`、`learning_manager.*`、`lang_bar_button.cpp`）を手本にする

## 共通の型（契約）

- [user_dictionary.h](../../core/include/astelio/user_dictionary.h) と [user_dictionary_io.h](../../core/include/astelio/user_dictionary_io.h) の宣言に合わせて書く。どちらも変えない
- 中身（`user_dictionary.cpp`、`user_dictionary_io.cpp`）と、変換にユーザー辞書を渡す関数（`Converter` / `InputSession` の `SetUserDictionary` など）は、ほかの2つの作業が並行して作っている。それらが `main` に入るまで、この作業のCIはリンクで失敗してよい
- 入った後に `origin/main` を取り込み、実際の関数名に合わせてからCIを通す。関数名が分からないうちは、渡す部分を1か所にまとめておく

## やること

1. `user_dictionary_store.*`: `%APPDATA%\AstelioIME\user_dictionary.tsv` の読み書き。入力履歴と同じく、一時ファイル経由で全体を書き直し、入力の始めに更新時刻を見て読み直す。テスト用にファイルの場所を差し替える入口（`AstelioTipTestUseUserDictionary` など）を用意する
2. `text_service.cpp`: 読み込んだユーザー辞書を変換に渡す。別のアプリで変えた内容も次の入力から使う
3. `user_dictionary_manager.*`: 管理画面（入力履歴の画面と同じ作り）
   - 一覧（読み・表記・品詞・コメント）、検索欄（部分一致）、追加・編集（読み・表記・品詞・コメントの入力欄。`UserDictionary::Valid` で確かめる）・削除
   - 「インポート...」「エクスポート...」: ファイルを選び、`DetectEncoding` が Shift_JIS なら code page 932 で読み、それ以外は `DecodeText`。取り込んだ件数と飛ばした行数を表示する。書き出しは形式を選び、`ExportEncoding` の文字コードで書く
   - 取り込みで壊れたファイルを渡しても、既存の辞書を壊さない（F-04の方針）
4. `lang_bar_button.cpp`: 右クリックメニューに「ユーザー辞書...」を加える
5. テストを先に書く（`platform/windows/tip/tests/tip_integration_test.cpp`）
   - T-D02-2: テスト用のファイルに語を書いて保存すると、次の入力でその語が変換の先頭に出る。ファイルを書き換える（別のアプリの変更に当たる）と読み直される
   - T-D03-3: Shift_JIS のファイルを読む処理と、書き出したファイルをもう一度取り込むと同じ語になること（画面を通さない関数として分けてテストする）
   - 画面の操作は手動のテストとして、確認の手順をPRに書く
6. 文書: README（キー操作の後の説明か入力の履歴の行）、計画書の12章（D-02・D-03・D-08を「済」にし、残った課題を書く）、テスト計画書の T-D02-2・T-D03-3 の行

## 触ってよい範囲（ほかの作業とぶつからないように）

- 変更してよい: `platform/windows/tip/` の中、README、計画書の12章と4.6、テスト計画書の T-D02-2・T-D03-3 の行
- 変更しない: `core/`（ほかの2つの作業）。コアの不具合を見つけたら、直さずに報告する

## 進め方

- 画面以外の部分はテストを先に書く（このPCではC++をビルドできない。CIで確かめる）
- 同じ方法で2回失敗したら止めて、原因と次の方針を報告する
- マージの条件: コアと取り込みの2つのPRが `main` に入っていること、`origin/main` を取り込んだ後にCIがすべて成功すること、追加したテストが実行されたことをログで確かめたこと。そろったら `gh pr merge --merge --delete-branch` でマージする
- ほかの作業がまだ終わっていないときは、PRを作ったところで止め、待っていることを報告する

最後に、通ったテストID、PRのURL、変更したファイル、手動で確認する手順（TIPの取得と登録: `./tools/Get-AstelioTip.ps1`、管理者で `./tools/Register-AstelioTip.ps1 -Path artifacts/tip`）、残った課題を報告してください。
