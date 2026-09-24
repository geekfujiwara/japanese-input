---
description: "Use when writing C++ for the Astelio conversion engine (core) or conversion server: romaji composer, symbol rules, dictionary lookup, lattice/Viterbi, learning, prediction, IPC."
applyTo: ["core/**", "server/**"]
---
# C++ Core / 変換サーバー

- C++20。`core/` はOSに依存しない（Win32・Cocoaのヘッダーを含めない）。OS依存の処理は `platform/` か `server/` に置く
- 外部に公開する境界はC ABIとし、例外を境界の外に出さない。エラーは戻り値で返す
- 文字列は内部でUTF-16、ファイルとIPCではUTF-8。変換は境界で1回だけ行う
- 1キーの処理と変換は、計画書3.5の性能目標（16ms / 30ms）を超えないこと。重い処理を追加したらベンチマークも追加する
- テストはGoogleTest、ベンチマークはGoogle Benchmark。ローマ字変換・辞書読み込み・IPCメッセージの入口にはファジングの入口関数を用意する
- 旧版の `InputNormalizer` と `AltPressTracker` のテストケースは、移植後も同じ期待結果を保つ
- 入力内容をログに出さない。デバッグ用の出力も文字数や種別だけにする
