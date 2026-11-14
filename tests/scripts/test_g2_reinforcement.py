import json
import runpy
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE = runpy.run_path(str(ROOT / "scripts" / "run-g2-reinforcement.py"))
grade = MODULE["grade"]


class G2ReinforcementTests(unittest.TestCase):
    def test_grades_three_controlled_questions(self):
        self.assertEqual(grade("sky_is_blue", "Sim, o céu é azul!")[0:2], ("correct", 1))
        self.assertEqual(grade("mountains_high_or_low", "As montanhas são altas.")[0:2], ("correct", 1))
        self.assertEqual(grade("grass_color", "A grama é verde.")[0:2], ("correct", 1))

    def test_rejects_negation_and_wrong_concept(self):
        self.assertEqual(grade("sky_is_blue", "Não, o céu não é azul.")[0:2], ("incorrect", -1))
        self.assertEqual(grade("mountains_high_or_low", "As montanhas são baixas.")[0:2], ("incorrect", -1))

    def test_database_is_isolated_and_has_2000_parameter_policy(self):
        db = json.loads(
            (ROOT / "data" / "reinforcement-g2" / "answers.json").read_text(encoding="utf-8")
        )
        self.assertEqual(db["policy"]["trainable_parameters"], 2000)
        self.assertEqual(db["policy"]["experts"], {"expert_a": 1000, "expert_b": 1000})
        episodes = db["episodes"]
        totals = db["totals"]
        self.assertEqual(totals["episodes"], len(episodes))
        self.assertEqual(totals["correct"] + totals["incorrect"], len(episodes))
        self.assertEqual(totals["policy_updates"] + totals["skipped_updates"], len(episodes))
        episode_ids = [episode["episode_id"] for episode in episodes]
        self.assertEqual(len(episode_ids), len(set(episode_ids)))
        for episode in episodes:
            self.assertEqual(episode["source"], "model_generated")
            self.assertEqual(episode["trainable_parameters"], 2000)
            self.assertIn(episode["question_id"], {"sky_is_blue", "mountains_high_or_low", "grass_color"})
            self.assertIn(episode["verdict"], {"correct", "incorrect"})
            self.assertEqual(episode["reward"], 1 if episode["verdict"] == "correct" else -1)


if __name__ == "__main__":
    unittest.main()
