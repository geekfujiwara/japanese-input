"""T-S06: scores "もしかして" methods on the typo corpus and records the results.

The dictionary proposes the words one key slip away (astelio_typo writes candidates.json); a method picks one or none.
  algo-cost    the cheapest candidate when the reading as typed is not a word
  algo-margin  also when the cheapest candidate is much more common than the word typed
  slm          a llama-server model chooses among the candidates (only called when there are candidates)

Standard library only. Usage:
    python3 slm/evaluate.py --method algo-cost --name algo-cost --candidates candidates.json --output result.json
    python3 slm/evaluate.py --method slm --name qwen3-0.6b --candidates candidates.json --output result.json \
        [--server http://127.0.0.1:8080] [--server-pid 1234] [--model-bytes 675710816]
"""

import argparse
import json
import os
import re
import statistics
import sys
import time
import urllib.request

# Word costs are integers where lower is more likely; the typo penalty is already in a candidate's cost.
MARGIN = 1000
MAX_OPTIONS = 5

SYSTEM_PROMPT = (
    "あなたは日本語入力システムの打ち間違い判定です。利用者はローマ字でかなを入力します。"
    "前の文脈と入力された読みを見て、利用者が本当に入力したかった語を選択肢から選び、番号だけを答えてください。"
    "入力された読みのままで正しいときは0と答えてください。"
)

# Few-shot examples that are not in the corpus: (context, typed option, candidates, answer).
EXAMPLES = [
    ("図書館で", "ほんをかりら", ["本を借りた", "本を刈りら"], "1"),
    ("明日の", "天気", ["電気", "転記"], "0"),
    ("会社の", "かいぎ", ["会議", "懐疑"], "1"),
]


def options_message(context, typed, candidates):
    lines = [f"前の文脈: {context or '（なし）'}", f"0: {typed}（入力のまま）"]
    lines += [f"{number}: {surface}" for number, surface in enumerate(candidates, 1)]
    lines.append("番号:")
    return "\n".join(lines)


def cpu_ms(pid):
    try:
        with open(f"/proc/{pid}/stat", encoding="ascii") as file:
            fields = file.read().rsplit(")", 1)[1].split()
        ticks = int(fields[11]) + int(fields[12])  # utime + stime
        return ticks * 1000 / os.sysconf("SC_CLK_TCK")
    except (OSError, ValueError, IndexError):
        return None


def peak_memory_mb(pid):
    try:
        with open(f"/proc/{pid}/status", encoding="ascii") as file:
            for line in file:
                if line.startswith("VmHWM:"):
                    return int(line.split()[1]) / 1024
    except OSError:
        pass
    return None


def ask_model(server, context, typed, candidates):
    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    for example_context, example_typed, example_candidates, answer in EXAMPLES:
        messages.append({"role": "user", "content": options_message(example_context, example_typed, example_candidates)})
        messages.append({"role": "assistant", "content": answer})
    messages.append({"role": "user", "content": options_message(context, typed, candidates)})
    body = json.dumps({
        "messages": messages,
        "temperature": 0,
        "max_tokens": 4,
        "cache_prompt": True,
        "chat_template_kwargs": {"enable_thinking": False},
    }).encode("utf-8")
    request = urllib.request.Request(f"{server}/v1/chat/completions", data=body,
                                     headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=120) as response:
        reply = json.load(response)
    content = re.sub(r"<think>.*?</think>", "", reply["choices"][0]["message"]["content"], flags=re.S)
    digits = re.findall(r"\d", content)
    choice = int(digits[0]) if digits else 0
    return choice if 0 <= choice <= len(candidates) else 0


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(fraction * (len(ordered) - 1))))] if ordered else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--method", required=True, choices=["algo-cost", "algo-margin", "slm"])
    parser.add_argument("--name", required=True)
    parser.add_argument("--candidates", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--server", default="http://127.0.0.1:8080")
    parser.add_argument("--server-pid", type=int)
    parser.add_argument("--model-bytes", type=int)
    arguments = parser.parse_args()

    with open(arguments.candidates, encoding="utf-8") as file:
        data = json.load(file)
    entries = data["entries"]

    if arguments.method == "slm":
        ask_model(arguments.server, "", "あ", ["亜"])  # warm-up: the first request pays for loading
    rows = []
    latencies = []
    cpu_costs = []
    for entry in entries:
        candidates = [c["surface"] for c in entry["candidates"]][:MAX_OPTIONS]
        suggestion = None
        called = False
        if candidates:
            if arguments.method == "algo-cost":
                suggestion = candidates[0] if entry["typed_cost"] is None else None
            elif arguments.method == "algo-margin":
                best = entry["candidates"][0]["cost"]
                typed = entry["typed_cost"]
                suggestion = candidates[0] if typed is None or best + MARGIN < typed else None
            else:
                called = True
                before = cpu_ms(arguments.server_pid) if arguments.server_pid else None
                started = time.perf_counter()
                choice = ask_model(arguments.server, entry["context"], entry["typed_surface"], candidates)
                latencies.append((time.perf_counter() - started) * 1000)
                after = cpu_ms(arguments.server_pid) if arguments.server_pid else None
                if before is not None and after is not None:
                    cpu_costs.append(after - before)
                suggestion = candidates[choice - 1] if choice > 0 else None
        if entry["kind"] == "typo":
            ok = suggestion == entry["expected"]
        else:
            ok = suggestion is None
        rows.append({"kind": entry["kind"], "context": entry["context"], "reading": entry["reading"],
                     "expected": entry["expected"], "candidates": candidates, "answer": suggestion or "なし",
                     "ok": ok, "called": called})

    typos = [row for row in rows if row["kind"] == "typo"]
    corrects = [row for row in rows if row["kind"] == "correct"]
    result = {
        "name": arguments.name,
        "method": arguments.method,
        "typo_hit_rate": 100 * sum(row["ok"] for row in typos) / max(1, len(typos)),
        "false_suggestion_rate": 100 * sum(not row["ok"] for row in corrects) / max(1, len(corrects)),
        "candidate_recall": 100 * sum(row["expected"] in row["candidates"] for row in typos) / max(1, len(typos)),
        "call_rate": 100 * sum(row["called"] for row in rows) / max(1, len(rows)),
        "typo_count": len(typos),
        "correct_count": len(corrects),
        "latency_p50_ms": statistics.median(latencies) if latencies else None,
        "latency_p95_ms": percentile(latencies, 0.95),
        "cpu_ms_per_call": statistics.mean(cpu_costs) if cpu_costs else None,
        "peak_memory_mb": peak_memory_mb(arguments.server_pid) if arguments.server_pid else None,
        "model_mb": arguments.model_bytes / 1024 / 1024 if arguments.model_bytes else None,
        "candidate_p95_us": data.get("candidate_p95_us"),
        "rows": rows,
    }
    with open(arguments.output, "w", encoding="utf-8") as file:
        json.dump(result, file, ensure_ascii=False, indent=1)
    print(f"{arguments.name}: hit {result['typo_hit_rate']:.1f}% false {result['false_suggestion_rate']:.1f}% "
          f"recall {result['candidate_recall']:.1f}% calls {result['call_rate']:.1f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
