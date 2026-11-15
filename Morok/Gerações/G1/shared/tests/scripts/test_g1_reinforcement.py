import json
import runpy
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
MODULE = runpy.run_path(str(ROOT / "G1" / "scripts" / "reinforcement.py"))
grade = MODULE["grade"]
select_question = MODULE["select_question"]
format_attempt_results = MODULE["format_attempt_results"]


class ReinforcementGraderTests(unittest.TestCase):
    def test_sky_accepts_yes_or_blue(self):
        self.assertEqual(grade("sky_is_blue", "Sim, o céu é azul!")[0:2], ("correct", 1))
        self.assertEqual(grade("sky_is_blue", "Azul.")[0:2], ("correct", 1))

    def test_sky_rejects_negation(self):
        self.assertEqual(grade("sky_is_blue", "Não, o céu não é azul.")[0:2], ("incorrect", -1))

    def test_mountains_accepts_high_not_low(self):
        self.assertEqual(grade("mountains_high_or_low", "As montanhas são altas.")[0:2], ("correct", 1))
        self.assertEqual(grade("mountains_high_or_low", "As montanhas são baixas.")[0:2], ("incorrect", -1))

    def test_grass_accepts_green(self):
        self.assertEqual(grade("grass_color", "A grama é verde.")[0:2], ("correct", 1))
        self.assertEqual(grade("grass_color", "A grama é amarela.")[0:2], ("incorrect", -1))

    def test_empty_answer_is_incorrect(self):
        self.assertEqual(grade("grass_color", "   ")[0:2], ("incorrect", -1))


    def test_attempts_rotate_through_all_three_questions_without_stopping(self):
        questions = [
            {"id": "sky_is_blue"},
            {"id": "mountains_high_or_low"},
            {"id": "grass_color"},
        ]
        selected = [select_question(questions, index)["id"] for index in range(8)]
        self.assertEqual(selected, [
            "sky_is_blue", "mountains_high_or_low", "grass_color",
            "sky_is_blue", "mountains_high_or_low", "grass_color",
            "sky_is_blue", "mountains_high_or_low",
        ])

    def test_attempt_report_shows_only_number_answer_and_verdict(self):
        lines = format_attempt_results([
            {"attempt": 1, "answer": "Sim.\n", "verdict": "correct"},
            {"attempt": 2, "answer": "Não sei.", "verdict": "incorrect"},
        ])
        self.assertEqual(lines, [
            "1. Sim. - correto",
            "2. Não sei. - errado",
        ])

    def test_database_has_exactly_three_tasks(self):
        database = json.loads((ROOT / "G1" / "data" / "reinforcement" / "answers.json").read_text(encoding="utf-8"))
        self.assertEqual({task["id"] for task in database["questions"]}, {
            "sky_is_blue", "mountains_high_or_low", "grass_color"
        })
        self.assertEqual(database["totals"]["episodes"], len(database["episodes"]))
        episode_ids = [episode.get("episode_id") for episode in database["episodes"]]
        self.assertEqual(len(episode_ids), len(set(episode_ids)))
        for episode in database["episodes"]:
            self.assertIn(episode["verdict"], {"correct", "incorrect"})
            self.assertIn(episode["reward"], {-1, 1})
            self.assertIn(episode["question_id"], {
                "sky_is_blue", "mountains_high_or_low", "grass_color"
            })


if __name__ == "__main__":
    unittest.main()
