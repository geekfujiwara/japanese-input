"""T-S06: merges the per-model results of slm/evaluate.py into one Markdown report.

Usage: python3 slm/summarize.py results/*.json > report.md
"""

import json
import sys


def number(value, digits=0):
    return "—" if value is None else f"{value:.{digits}f}"


def main(paths):
    results = []
    for path in paths:
        with open(path, encoding="utf-8") as file:
            results.append(json.load(file))
    results.sort(key=lambda r: (-r["typo_hit_rate"], r["false_suggestion_rate"], r["latency_p95_ms"]))

    lines = [
        "### SLMの比較（T-S06、もしかして）",
        "",
        "| モデル | もしかしての的中率 | 正しい読みへの誤提案率 | 遅延 p50 | 遅延 p95 | ピークメモリ | ファイル |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for r in results:
        lines.append(
            f"| {r['name']} | {number(r['typo_hit_rate'], 1)}%（{r['typo_count']}件） "
            f"| {number(r['false_suggestion_rate'], 1)}%（{r['correct_count']}件） "
            f"| {number(r['latency_p50_ms'])} ms | {number(r['latency_p95_ms'])} ms "
            f"| {number(r['peak_memory_mb'])} MB | {number(r['model_mb'])} MB |"
        )
    lines += ["", "CPUのみ（GitHub Actionsのubuntu-latest）、Q8_0、同じ指示文。", ""]
    for r in results:
        lines += [f"<details><summary>{r['name']} の回答</summary>", "", "| 種類 | 文脈 | 読み | 期待 | 回答 | |",
                  "| --- | --- | --- | --- | --- | --- |"]
        for row in r["rows"]:
            mark = "○" if row["ok"] else "×"
            lines.append(f"| {row['kind']} | {row['context']} | {row['reading']} | {row['expected']} "
                         f"| {row['answer'].replace('|', '｜')} | {mark} |")
        lines += ["", "</details>", ""]
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
