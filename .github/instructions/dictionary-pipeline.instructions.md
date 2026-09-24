---
description: "Use when building the autonomous dictionary creation environment: local SLM generation/checking/judging, confidence scoring, golden set, adoption gate, rollback, evaluation corpus, Copilot SDK harness, BYOK with Ollama or Foundry Local."
applyTo: ["dictionary/**", "eval/**"]
---
# 辞書作成環境（完全自律）

仕様は計画書の4.5.1〜4.5.3、テストはテスト計画書のT-A*・T-G*。

- 人の確認を前提にしない。確信度の低い語は評価AIが判定し、判断できないものは却下する
- 役割ごとに別系統のモデルを使う（生成AI: sarashina2.2-3b、照合AI: llm-jp-3-3.7b、評価AI: gpt-oss-20b）。エージェント型を試す場合、統括エージェントは評価AIと別のモデルにする
- モデルはOpenAI互換のAPIでローカルのサーバーに接続する。接続先・モデル名・版・ハッシュ・乱数の種を必ず記録する
- スキーマ（JSON Schema）を先に決め、ツールの入出力とモデルの出力はすべてスキーマで検証する。形式が崩れた出力は却下扱いにする
- 採用するかどうかは通常のプログラムが決める。モデルやエージェントに辞書ファイルを直接書かせない
- 評価コーパス、確認用の語、採用判定のプログラム、しきい値の計算は、生成・判定の処理から変更できないようにし、ハッシュを確認してから実行する
- 指示文は版を付けて管理し、変更したら確認用の語と評価コーパスで測り直す
- 受け入れテストは実装とは別のセッションで仕様から先に書き、実装中は変更しない
- エージェント型は標準にしない。ワークフロー型と比べて品質で上回った場合だけ採用する
