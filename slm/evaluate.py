"""T-S06: asks a running llama-server for "もしかして" guesses on the typo corpus and records the scores.

Standard library only. Usage:
    python3 slm/evaluate.py --name qwen2.5-0.5b --corpus slm/corpus/typo.tsv --output result.json \
        [--server http://127.0.0.1:8080] [--server-pid 1234] [--model-bytes 675710816]
"""

import argparse
import json
import re
import statistics
import sys
import time
import urllib.request

SYSTEM_PROMPT = (
    "あなたは日本語入力システムの打ち間違い補正です。"
    "利用者はローマ字でかなを入力します。入力された読みにキーの打ち間違い（抜け・余分・隣のキー）があると思われるときは、"
    "前の文脈から本来入力したかった語句を推測し、その表記（漢字かな交じり）だけを1行で答えてください。"
    "打ち間違いがないと思われるときは「なし」とだけ答えてください。説明は書かないでください。"
)

# Few-shot examples that are not in the corpus.
EXAMPLES = [
    ("図書館で", "ほんをかりら", "本を借りた"),
    ("明日の", "てんきよほう", "なし"),
]

NONE_ANSWER = "なし"


def read_corpus(path):
    entries = []
    with open(path, encoding="utf-8") as file:
        for number, line in enumerate(file, 1):
            line = line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) < 4 or fields[0] not in ("typo", "correct") or not fields[2] or not fields[3]:
                raise ValueError(f"{path}:{number}: expected kind, context, typed reading, intended text")
            entries.append({"kind": fields[0], "context": fields[1], "reading": fields[2], "expected": fields[3]})
    return entries


def user_message(context, reading):
    return f"前の文脈: {context or '（なし）'}\n入力された読み: {reading}"


def normalize(answer):
    answer = re.sub(r"<think>.*?</think>", "", answer, flags=re.S)
    # Some GGUF conversions print their end-of-sequence token as text.
    answer = re.sub(r"</?s>|<\|[^|<>]*\|>", "", answer)
    answer = answer.strip().splitlines()[0] if answer.strip() else ""
    answer = re.sub(r"^(答え|回答|表記)[:：]\s*", "", answer)
    return answer.strip().strip("「」『』\"'").rstrip("。.").strip()


def ask(server, context, reading):
    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    for example_context, example_reading, example_answer in EXAMPLES:
        messages.append({"role": "user", "content": user_message(example_context, example_reading)})
        messages.append({"role": "assistant", "content": example_answer})
    messages.append({"role": "user", "content": user_message(context, reading)})
    body = json.dumps({
        "messages": messages,
        "temperature": 0,
        "max_tokens": 32,
        "cache_prompt": True,
        "chat_template_kwargs": {"enable_thinking": False},
    }).encode("utf-8")
    request = urllib.request.Request(f"{server}/v1/chat/completions", data=body,
                                     headers={"Content-Type": "application/json"})
    started = time.perf_counter()
    with urllib.request.urlopen(request, timeout=120) as response:
        reply = json.load(response)
    elapsed_ms = (time.perf_counter() - started) * 1000
    return reply["choices"][0]["message"]["content"], elapsed_ms


def peak_memory_mb(pid):
    try:
        with open(f"/proc/{pid}/status", encoding="ascii") as file:
            for line in file:
                if line.startswith("VmHWM:"):
                    return int(line.split()[1]) / 1024
    except OSError:
        pass
    return None


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(fraction * (len(ordered) - 1))))]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--server", default="http://127.0.0.1:8080")
    parser.add_argument("--server-pid", type=int)
    parser.add_argument("--model-bytes", type=int)
    arguments = parser.parse_args()

    entries = read_corpus(arguments.corpus)
    ask(arguments.server, "", "あ")  # warm-up: the first request pays for loading
    rows = []
    latencies = []
    for entry in entries:
        raw, elapsed_ms = ask(arguments.server, entry["context"], entry["reading"])
        answer = normalize(raw)
        latencies.append(elapsed_ms)
        if entry["kind"] == "typo":
            ok = answer == entry["expected"]
        else:
            ok = answer in (NONE_ANSWER, entry["expected"], entry["reading"])
        rows.append({**entry, "answer": answer, "ok": ok, "ms": round(elapsed_ms, 1)})

    typos = [row for row in rows if row["kind"] == "typo"]
    corrects = [row for row in rows if row["kind"] == "correct"]
    result = {
        "name": arguments.name,
        "typo_hit_rate": 100 * sum(row["ok"] for row in typos) / max(1, len(typos)),
        "false_suggestion_rate": 100 * sum(not row["ok"] for row in corrects) / max(1, len(corrects)),
        "typo_count": len(typos),
        "correct_count": len(corrects),
        "latency_p50_ms": statistics.median(latencies),
        "latency_p95_ms": percentile(latencies, 0.95),
        "peak_memory_mb": peak_memory_mb(arguments.server_pid) if arguments.server_pid else None,
        "model_mb": arguments.model_bytes / 1024 / 1024 if arguments.model_bytes else None,
        "rows": rows,
    }
    with open(arguments.output, "w", encoding="utf-8") as file:
        json.dump(result, file, ensure_ascii=False, indent=1)
    print(f"{arguments.name}: hit {result['typo_hit_rate']:.1f}% false {result['false_suggestion_rate']:.1f}% "
          f"p50 {result['latency_p50_ms']:.0f}ms p95 {result['latency_p95_ms']:.0f}ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
