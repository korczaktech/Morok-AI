import json
import runpy
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE = runpy.run_path(str(ROOT / "scripts" / "run-g1-reinforcement.py"))
grade = MODULE["grade"]


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

    def test_database_has_exactly_three_tasks(self):
        database = json.loads((ROOT / "data" / "reinforcement" / "answers.json").read_text(encoding="utf-8"))
        self.assertEqual({task["id"] for task in database["questions"]}, {
            "sky_is_blue", "mountains_high_or_low", "grass_color"
        })
        self.assertEqual(database["totals"]["episodes"], len(database["episodes"]))
        for episode in database["episodes"]:
            self.assertIn(episode["verdict"], {"correct", "incorrect"})
            self.assertIn(episode["reward"], {-1, 1})
            self.assertIn(episode["question_id"], {
                "sky_is_blue", "mountains_high_or_low", "grass_color"
            })


if __name__ == "__main__":
    unittest.main()
