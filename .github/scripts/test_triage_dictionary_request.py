"""T-U02-1, T-U02-2: checking and exporting dictionary requests (run: python -m unittest discover .github/scripts)."""

import unittest

from triage_dictionary_request import export, parse_form, triage


def form(reading="あすてりお", surface="Astelio", pos="固有名詞", context="_No response_"):
    return (
        f"### 読み\n\n{reading}\n\n"
        f"### 変換してほしい表記\n\n{surface}\n\n"
        "### 今の変換結果\n\n_No response_\n\n"
        "### 種類\n\n新しい語を追加してほしい\n\n"
        f"### 品詞\n\n{pos}\n\n"
        f"### 使う場面・前後の文\n\n{context}\n"
    )


class TriageTest(unittest.TestCase):
    def test_parses_the_issue_form(self):
        fields = parse_form(form(context="一行目\r\n二行目"))
        self.assertEqual(fields["読み"], "あすてりお")
        self.assertEqual(fields["今の変換結果"], "")
        self.assertEqual(fields["使う場面・前後の文"], "一行目\n二行目")

    def test_a_well_formed_request_is_ready(self):
        request = triage(form())
        self.assertTrue(request.ready, request.errors)
        self.assertEqual((request.reading, request.surface, request.pos), ("あすてりお", "Astelio", "固有名詞"))

    def test_katakana_readings_become_hiragana(self):
        request = triage(form(reading="ヴァイオリン", surface="バイオリン"))
        self.assertTrue(request.ready, request.errors)
        self.assertEqual(request.reading, "ゔぁいおりん")

    def test_readings_must_be_hiragana(self):
        for reading in ["astelio", "明日", "あす てりお", ""]:
            with self.subTest(reading=reading):
                self.assertFalse(triage(form(reading=reading)).ready)

    def test_limits(self):
        self.assertTrue(triage(form(reading="あ" * 64)).ready)
        self.assertFalse(triage(form(reading="あ" * 65)).ready)
        self.assertTrue(triage(form(surface="字" * 128)).ready)
        self.assertFalse(triage(form(surface="字" * 129)).ready)
        self.assertFalse(triage(form(surface="😀" * 65)).ready, "counted in UTF-16 units like UserDictionary")
        self.assertFalse(triage(form(context="あ" * 1001)).ready)

    def test_surfaces_must_be_one_line_and_differ_from_the_reading(self):
        self.assertFalse(triage(form(surface="_No response_")).ready)
        self.assertFalse(triage(form(surface="Aste\tlio")).ready)
        self.assertFalse(triage(form(surface="あすてりお")).ready)

    def test_bodies_without_the_form_are_not_ready(self):
        request = triage("辞書に入れてください")
        self.assertFalse(request.ready)
        self.assertEqual(len(request.errors), 1)


class ExportTest(unittest.TestCase):
    def test_exports_ready_requests_in_issue_order(self):
        issues = [
            {"number": 12, "body": form(reading="くらうど", surface="クラウド", pos="わからない")},
            {"number": 3, "body": form()},
            {"number": 7, "body": form(reading="bad")},
            {"number": 9, "body": None},
        ]
        self.assertEqual(
            export(issues),
            ["あすてりお\tAstelio\t固有名詞\t3", "くらうど\tクラウド\t\t12"],
        )


if __name__ == "__main__":
    unittest.main()
