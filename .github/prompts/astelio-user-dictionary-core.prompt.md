---
description: "ユーザー辞書（コア）: UserDictionary の実装と、変換・予測・抑制語への反映（D-02・D-08）"
name: "astelio-user-dictionary-core"
agent: "agent"
model: "Claude Opus 5.5 (copilot)"
---
ユーザー辞書のコア部分を1回分の作業として実装してください。この作業は、ほかの2つの作業（`/astelio-user-dictionary-io`、`/astelio-user-dictionary-tip`）と並行して進みます。

## 対象

- 要件: D-02（追加・編集・削除・検索）、D-08（抑制語）
- テストID: T-D02-1、T-D08-1（[テスト計画書](../../docs/astelio-ime-test-plan.md)）
- ブランチ: `phase5/user-dictionary-core`（最新の `main` から作る）
- 計画書で読む節: 2章のD-02・D-08、4.6（データの保存）、12章

## 共通の型（契約）

- [core/include/astelio/user_dictionary.h](../../core/include/astelio/user_dictionary.h) の公開部分（型・関数の宣言・コメントに書いた動作）は、ほかの2つの作業が使います。変えないでください。private のメンバーは足してかまいません
- 公開部分を変えないと実装できないときは、止めて理由を報告してください

## やること

1. `core/src/user_dictionary.cpp` に、ヘッダーで宣言した関数をすべて実装する（`Serialize` / `Parse` の形式はヘッダーのコメントのとおり。`Parse` は壊れた行を飛ばし、例外を投げない）
2. 変換に反映する
   - `Converter` と `InputSession` にユーザー辞書を渡せるようにする（例: `SetUserDictionary(const UserDictionary*)`、nullptr で外す）
   - 読みがちょうど一致する文節では、ユーザー辞書の語を候補の先頭に出す。文の中の語としても選ばれるように、ラティスに語として入れる（品詞は名詞相当の連接IDを使い、短縮よみは文節全体の読みと一致したときだけ出す、など。方法は任せる）
   - 予測（B-04）にも `Predict` の語を出す
3. 抑制語（D-08）: `Suppressed` に当たる候補は、変換・候補一覧・予測・入力の履歴による並べ替え・もしかして（B-14）のどこにも出さない。文節の最善の候補が抑制されたら次の候補を使う
4. 優先順位: 抑制語 ＞ 入力の履歴（D-04）＞ ユーザー辞書 ＞ システム辞書。この順を計画書のD-02の説明に1行で書く
5. テストを先に書く: `core/tests/user_dictionary_test.cpp`（追加・更新・削除・検索・上限・Serialize/Parse の往復・壊れた行）と、`input_session_test.cpp` か `converter_test.cpp` に T-D02-1（追加した語が次の変換で先頭に出る、削除すると戻る）と T-D08-1（抑制した語が変換・予測に出ない）を加える
6. 評価コーパスの正解率（CIの System dictionary ジョブ）が下がらないことを確認する（ユーザー辞書が空なら結果は変わらないはず）

## 触ってよい範囲（ほかの作業とぶつからないように）

- 変更してよい: `core/src/user_dictionary.cpp`（新規）、`core/src/converter.cpp`、`core/src/input_session.cpp`、`core/include/astelio/converter.h`、`core/include/astelio/input_session.h`、`core/tests/user_dictionary_test.cpp`（新規）、`core/tests/input_session_test.cpp`、`core/tests/converter_test.cpp`、`core/CMakeLists.txt`、`core/tests/CMakeLists.txt`
- 文書は、テスト計画書の T-D02-1・T-D08-1 の行と、計画書のD-02・D-08の行だけを直す（12章とREADMEはTIPの作業がまとめて直す）
- 変更しない: `core/src/user_dictionary_io.cpp` とそのテスト（取り込みの作業）、`platform/`（TIPの作業）

## 進め方

- テストを先に書き、CIで失敗を確認してから実装する（このPCではC++をビルドできない。CIで確かめる）
- 同じ方法で2回失敗したら止めて、原因と次の方針を報告する
- CMakeLists のソースの並びは、ほかの作業と同じ場所に行を足すことがある。マージの前に `origin/main` を取り込み、ぶつかったら両方の行を残す
- CIがすべて成功し、追加したテストが実行されたことをログで確かめたら、`gh pr merge --merge --delete-branch` でマージする。この作業は3つのうち最初にマージしてよい

最後に、通ったテストID、PRのURL、変更したファイル、`Converter` / `InputSession` に足した関数（TIPの作業が使う）、残った課題を報告してください。
