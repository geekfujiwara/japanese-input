"""T-S06: merges the results of slm/evaluate.py into one Markdown report.

Usage: python3 slm/summarize.py results/*.json > report.md
"""

import json
import sys


def number(value, digits=0, unit=""):
    return "—" if value is None else f"{value:.{digits}f}{unit}"


def main(paths):
    results = []
    for path in paths:
        with open(path, encoding="utf-8") as file:
            results.append(json.load(file))
    results.sort(key=lambda r: (-(r["typo_hit_rate"] - r["false_suggestion_rate"]), r["latency_p95_ms"] or 0))
    first = results[0] if results else {}

    lines = [
        "### もしかしての比較（T-S06）",
        "",
        f"辞書が作る候補に正解が入る割合: {number(first.get('candidate_recall'), 1, '%')}"
        f"（候補の生成 p95: {number(first.get('candidate_p95_us'), 0, ' µs')}）",
        "",
        "| 方式 | 的中率 | 正しい読みへの誤提案率 | SLMの呼び出し | 遅延 p50 / p95（1回） | CPU時間（1回） | ピークメモリ | ファイル |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for r in results:
        lines.append(
            f"| {r['name']} | {number(r['typo_hit_rate'], 1, '%')}（{r['typo_count']}件） "
            f"| {number(r['false_suggestion_rate'], 1, '%')}（{r['correct_count']}件） "
            f"| {number(r['call_rate'], 0, '%')} "
            f"| {number(r['latency_p50_ms'], 0)} / {number(r['latency_p95_ms'], 0, ' ms')} "
            f"| {number(r['cpu_ms_per_call'], 0, ' ms')} "
            f"| {number(r['peak_memory_mb'], 0, ' MB')} | {number(r['model_mb'], 0, ' MB')} |"
        )
    lines += ["", "SLMはCPUのみ・2スレッド（GitHub Actionsのubuntu-latest）で測定。algo-* はSLMを使わない。", ""]
    for r in results:
        lines += [f"<details><summary>{r['name']} の回答</summary>", "",
                  "| 種類 | 文脈 | 読み | 期待 | 候補 | 回答 | |", "| --- | --- | --- | --- | --- | --- | --- |"]
        for row in r["rows"]:
            mark = "○" if row["ok"] else "×"
            candidates = "、".join(row["candidates"]) or "—"
            lines.append(f"| {row['kind']} | {row['context']} | {row['reading']} | {row['expected']} "
                         f"| {candidates} | {row['answer']} | {mark} |")
        lines += ["", "</details>", ""]
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
