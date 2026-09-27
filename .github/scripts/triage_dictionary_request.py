"""Checks the dictionary requests filed with the GitHub issue form (U-02) and exports the ready ones.

    python triage_dictionary_request.py triage < body.md    # prints "ready" or "needs-info", writes comment.md
    gh issue list --label dictionary:ready --state open --json number,body --limit 1000 \
        | python triage_dictionary_request.py export > requests.tsv

The body is untrusted: it is only parsed, never run or put into a shell command.
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass, field

# Same limits as UserDictionary (core/include/astelio/user_dictionary.h), counted in UTF-16 units.
MAX_READING = 64
MAX_SURFACE = 128
MAX_CONTEXT = 1000

READING = "読み"
SURFACE = "変換してほしい表記"
CURRENT = "今の変換結果"
KIND = "種類"
POS = "品詞"
CONTEXT = "使う場面・前後の文"

NO_RESPONSE = "_No response_"
UNKNOWN_POS = "わからない"


@dataclass
class Request:
    reading: str = ""
    surface: str = ""
    current: str = ""
    kind: str = ""
    pos: str = ""
    context: str = ""
    errors: list[str] = field(default_factory=list)

    @property
    def ready(self) -> bool:
        return not self.errors


def parse_form(body: str) -> dict[str, str]:
    """Splits an issue form body ("### label" then the value) into label -> value."""
    fields: dict[str, str] = {}
    label = None
    lines: list[str] = []
    for line in body.replace("\r\n", "\n").split("\n"):
        if line.startswith("### "):
            if label is not None:
                fields[label] = "\n".join(lines).strip()
            label = line[4:].strip()
            lines = []
        elif label is not None:
            lines.append(line)
    if label is not None:
        fields[label] = "\n".join(lines).strip()
    return {key: ("" if value == NO_RESPONSE else value) for key, value in fields.items()}


def utf16_length(text: str) -> int:
    return len(text.encode("utf-16-le")) // 2


def to_hiragana(text: str) -> str:
    """Katakana to hiragana (ァ..ヶ); the long vowel mark ー stays."""
    return "".join(chr(ord(c) - 0x60) if "ァ" <= c <= "ヶ" else c for c in text)


def is_reading(text: str) -> bool:
    return all("ぁ" <= c <= "ゖ" or c in "ーゝゞ" for c in text)


def has_control(text: str) -> bool:
    return any(ord(c) < 0x20 or ord(c) == 0x7F for c in text)


def triage(body: str) -> Request:
    fields = parse_form(body)
    request = Request(
        reading=to_hiragana(fields.get(READING, "").strip()),
        surface=fields.get(SURFACE, "").strip(),
        current=fields.get(CURRENT, "").strip(),
        kind=fields.get(KIND, "").strip(),
        pos=fields.get(POS, "").strip(),
        context=fields.get(CONTEXT, "").strip(),
    )
    if READING not in fields or SURFACE not in fields:
        request.errors.append("フォームの「読み」と「変換してほしい表記」の欄が見つかりません。テンプレートから起票してください。")
        return request
    if not request.reading:
        request.errors.append("「読み」を書いてください。")
    elif not is_reading(request.reading):
        request.errors.append("「読み」はひらがな（と「ー」）だけで書いてください。")
    elif utf16_length(request.reading) > MAX_READING:
        request.errors.append(f"「読み」は{MAX_READING}文字以内にしてください。")
    if not request.surface:
        request.errors.append("「変換してほしい表記」を書いてください。")
    elif has_control(request.surface):
        request.errors.append("「変換してほしい表記」には改行やタブを入れないでください。")
    elif utf16_length(request.surface) > MAX_SURFACE:
        request.errors.append(f"「変換してほしい表記」は{MAX_SURFACE}文字以内にしてください。")
    elif request.surface == request.reading:
        request.errors.append("「変換してほしい表記」が読みと同じです。変換したい形を書いてください。")
    if utf16_length(request.context) > MAX_CONTEXT:
        request.errors.append(f"「使う場面・前後の文」は{MAX_CONTEXT}文字以内にしてください。")
    return request


def comment(request: Request) -> str:
    if request.ready:
        return (
            "要望を受け付けました。ありがとうございます。\n\n"
            "辞書作成環境で照合・評価を行い、採用の判定を通った語を次の辞書に加えます。"
            "採用されなかった場合も、このIssueで理由をお知らせします。"
        )
    items = "\n".join(f"- {error}" for error in request.errors)
    return (
        "要望の形を確かめたところ、次の点を直す必要があります。"
        "Issueの本文を編集して直すと、もう一度確かめます。\n\n" + items
    )


def tsv_field(text: str) -> str:
    return " ".join(text.split())


def export(issues: list[dict]) -> list[str]:
    """Ready requests as "reading, surface, part of speech, issue number" lines (tab separated)."""
    lines = []
    for issue in sorted(issues, key=lambda i: i["number"]):
        request = triage(issue.get("body") or "")
        if not request.ready:
            continue
        pos = "" if request.pos in ("", UNKNOWN_POS) else request.pos
        lines.append("\t".join([request.reading, request.surface, tsv_field(pos), str(issue["number"])]))
    return lines


def main(argv: list[str]) -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stdin.reconfigure(encoding="utf-8")
    if len(argv) == 2 and argv[1] == "triage":
        request = triage(sys.stdin.read())
        with open("comment.md", "w", encoding="utf-8") as file:
            file.write(comment(request))
        print("ready" if request.ready else "needs-info")
        return 0
    if len(argv) == 2 and argv[1] == "export":
        for line in export(json.load(sys.stdin)):
            print(line)
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
