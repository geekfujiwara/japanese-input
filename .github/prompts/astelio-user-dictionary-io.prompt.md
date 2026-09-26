---
description: "ユーザー辞書（取り込み・書き出し）: 他のIMEの辞書ファイルと独自JSONの読み書き（D-03）"
name: "astelio-user-dictionary-io"
agent: "agent"
model: "Claude Opus 5.5 (copilot)"
---
ユーザー辞書の取り込み・書き出しを1回分の作業として実装してください。この作業は、ほかの2つの作業（`/astelio-user-dictionary-core`、`/astelio-user-dictionary-tip`）と並行して進みます。

## 対象

- 要件: D-03（Google日本語入力、MS-IME、ATOK、ことえりのテキスト形式、独自JSON）
- テストID: T-D03-1、T-D03-2、F-04（[テスト計画書](../../docs/astelio-ime-test-plan.md)）
- ブランチ: `phase5/user-dictionary-io`（最新の `main` から作る）
- 計画書で読む節: 2章のD-03、12章。テスト計画書の4.4（辞書）と F-04

## 共通の型（契約）

- [core/include/astelio/user_dictionary_io.h](../../core/include/astelio/user_dictionary_io.h) の公開部分は、TIPの作業が使います。変えないでください。変えないと実装できないときは、止めて理由を報告してください
- 語は `UserDictionary::Word`（[user_dictionary.h](../../core/include/astelio/user_dictionary.h)）で扱う。使ってよいのは `Word`・品詞の名前・`Valid` など、ヘッダーの中だけで動くものに限る（`UserDictionary` のメンバー関数はコアの作業が並行して実装中）
- 文字コードの変換は既存の [utf.h](../../core/include/astelio/utf.h) を使う。Shift_JIS はコアでは扱わない（`DecodeText` は nullopt を返し、TIPが code page 932 で読む）

## やること

1. 各形式の仕様を公開されている説明から確かめ、`core/tests/data/user_dictionary/` に形式ごとの小さなサンプルを自分で作る。他社の辞書データや製品のファイルをそのまま写さない
2. 品詞の対応表を決め、テスト計画書の T-D03-1 の下に表で書く。契約の8品詞にない品詞（動詞・形容詞など）は、名詞にするか飛ばすかを形式ごとに決めて表に書く。抑制語がある形式は `Suppressed` にする
3. テストを先に書く: `core/tests/user_dictionary_io_test.cpp`
   - T-D03-1: 形式ごとに全件が取り込まれ、品詞が表のとおりになる。形式の判定が正しい
   - T-D03-2: UTF-8（BOMあり・なし）、UTF-16LE/BE（BOMあり）の判定と復号、Shift_JIS の判定。不正な行は飛ばし、`skipped` に数える
   - 書き出した文字列を取り込むと同じ語に戻る（形式ごと。その形式で表せない品詞は表のとおりに変わってよい）
4. `core/src/user_dictionary_io.cpp` に実装する。JSON は必要な分だけの小さなパーサーを書く（外部ライブラリを足さない）。どんな入力でも落ちず、例外を投げない
5. F-04: `core/fuzz/user_dictionary_io_fuzzer.cpp` を足し、`core/CMakeLists.txt` の fuzzers と CI のfuzzingジョブ（`.github/workflows/ci.yml`）に加える

## 触ってよい範囲（ほかの作業とぶつからないように）

- 変更してよい: `core/src/user_dictionary_io.cpp`（新規）、`core/tests/user_dictionary_io_test.cpp`（新規）、`core/tests/data/user_dictionary/`（新規）、`core/fuzz/user_dictionary_io_fuzzer.cpp`（新規）、`core/CMakeLists.txt`、`core/tests/CMakeLists.txt`、`.github/workflows/ci.yml` のfuzzingジョブ
- 文書は、テスト計画書の T-D03-1・T-D03-2・F-04 の行と品詞の対応表、計画書のD-03の行だけを直す（12章とREADMEはTIPの作業がまとめて直す）
- 変更しない: `core/src/user_dictionary.cpp`・`converter`・`input_session`（コアの作業）、`platform/`（TIPの作業）

## 進め方

- テストを先に書き、CIで失敗を確認してから実装する（このPCではC++をビルドできない。CIで確かめる）
- 同じ方法で2回失敗したら止めて、原因と次の方針を報告する
- CMakeLists のソースの並びは、ほかの作業と同じ場所に行を足すことがある。マージの前に `origin/main` を取り込み、ぶつかったら両方の行を残す
- CIがすべて成功し、追加したテストとfuzzerが実行されたことをログで確かめたら、`gh pr merge --merge --delete-branch` でマージする。コアの作業より先に終わってもマージしてよい

最後に、通ったテストID、PRのURL、変更したファイル、品詞の対応表、形式ごとの書き出しの文字コード（`ExportEncoding`）、残った課題を報告してください。
