import json
import runpy
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE = runpy.run_path(str(ROOT / "scripts" / "run-g2-reinforcement.py"))
grade = MODULE["grade"]
select_questions = MODULE["select_questions"]
is_degenerate_response = MODULE["is_degenerate_response"]
select_training_target = MODULE["select_training_target"]
SEED_ANSWERS = MODULE["SEED_ANSWERS"]


class G2ReinforcementTests(unittest.TestCase):
    def test_grades_complete_controlled_answers(self):
        self.assertEqual(grade("sky_is_blue", "Sim, o céu é azul!")[0:2], ("correct", 1))
        self.assertEqual(grade("mountains_high_or_low", "As montanhas são altas.")[0:2], ("correct", 1))
        self.assertEqual(grade("grass_color", "A grama é verde.")[0:2], ("correct", 1))

    def test_rejects_incomplete_answers(self):
        self.assertEqual(grade("sky_is_blue", "Azul.")[0:2], ("incorrect", -1))
        self.assertEqual(grade("mountains_high_or_low", "São altas.")[0:2], ("incorrect", -1))
        self.assertEqual(grade("grass_color", "Verde.")[0:2], ("incorrect", -1))

    def test_rejects_negation_and_wrong_concept(self):
        self.assertEqual(grade("sky_is_blue", "Não, o céu não é azul.")[0:2], ("incorrect", -1))
        self.assertEqual(grade("mountains_high_or_low", "As montanhas são baixas.")[0:2], ("incorrect", -1))

    def test_degenerate_output_detector_rejects_repeated_characters(self):
        self.assertTrue(is_degenerate_response("aaaaaaaaaaaaaaaaaaaaaaaa"))
        self.assertTrue(is_degenerate_response("eeeeeeeeeeaeeeeeeeeeeee"))
        self.assertFalse(is_degenerate_response("Sim, o céu é azul."))
        self.assertFalse(is_degenerate_response("altas"))

    def test_wrong_generations_use_known_answer_as_oracle_target(self):
        self.assertEqual(select_training_target("sky_is_blue", "aae aae*ae", "incorrect"),
                         (SEED_ANSWERS["sky_is_blue"], "oracle_demonstration"))
        self.assertEqual(select_training_target("grass_color", "aaaaaaaaaaaaaaaa", "incorrect"),
                         (SEED_ANSWERS["grass_color"], "oracle_demonstration"))

    def test_correct_generation_uses_its_own_response_as_target(self):
        self.assertEqual(select_training_target("sky_is_blue", "Sim, o ceu e azul", "correct"),
                         ("Sim, o ceu e azul", "model_response"))


    def test_runner_uses_adaptive_training_and_reports_sequence_loss(self):
        runner = (ROOT / "scripts" / "run-g2-reinforcement.py").read_text(encoding="utf-8")
        app = (ROOT / "apps" / "reinforce_g2.cpp").read_text(encoding="utf-8")
        self.assertIn('default=1000)', runner)
        self.assertIn('MAX_BOOTSTRAP_EPOCHS=1000', runner)
        self.assertIn('bootstrap_epochs<=MAX_BOOTSTRAP_EPOCHS', runner)
        self.assertIn('default=.001)', runner)
        self.assertIn('str(a.learning_rate),"100"', runner)
        self.assertIn('"--patience",str(a.bootstrap_epochs)', runner)
        workflow = (ROOT / ".github" / "workflows" / "reinforcement-g2.yml").read_text(encoding="utf-8")
        self.assertIn('inputs.epochs || 1000', workflow)
        self.assertIn("if-no-files-found: warn", workflow)
        self.assertIn('target_loss_before', app)
        self.assertIn('target_loss_after', app)
        self.assertIn('G1OptimizerConfig::adamw()', app)

    def test_runner_isolates_new_checkpoints_and_requires_loss_progress(self):
        runner = (ROOT / "scripts" / "run-g2-reinforcement.py").read_text(encoding="utf-8")
        app = (ROOT / "apps" / "reinforce_g2.cpp").read_text(encoding="utf-8")
        self.assertIn('checkpoint=Path(str(a.checkpoint)+".v2")', runner)
        self.assertIn('if not target_loss_decreased:', runner)
        self.assertIn('target_loss_before', app)
        self.assertIn('target_loss_after', app)
        self.assertIn('for(unsigned int retry=0;retry<10;++retry)', app)

    def test_question_selection_keeps_database_order_and_rejects_empty_or_unknown(self):
        questions = [{"id": "sky_is_blue"}, {"id": "grass_color"}, {"id": "mountains_high_or_low"}]
        self.assertEqual([q["id"] for q in select_questions(questions, ["grass_color", "sky_is_blue"])],
                         ["sky_is_blue", "grass_color"])
        with self.assertRaises(ValueError):
            select_questions(questions, [])
        with self.assertRaises(ValueError):
            select_questions(questions, ["unknown"])
        with self.assertRaises(ValueError):
            select_questions(questions, ["sky_is_blue", "sky_is_blue"])

    def test_single_corpus_contains_all_three_feedback_prompts_and_targets(self):
        fixture_dir = ROOT / "tests" / "fixtures" / "g2-single-document"
        documents = sorted(fixture_dir.glob("*.txt"))
        self.assertEqual(len(documents), 1)
        corpus = documents[0].read_text(encoding="utf-8")
        expected = {
            "sky_is_blue": ("Usuario: O ceu e azul?\nIA: ", "Sim, o ceu e azul"),
            "mountains_high_or_low": ("Usuario: As montanhas sao altas ou baixas?\nIA: ", "As montanhas sao altas."),
            "grass_color": ("Usuario: Que cor e a grama?\nIA: ", "A grama e verde."),
        }
        db = json.loads((ROOT / "data" / "reinforcement-g2" / "answers.json").read_text(encoding="utf-8"))
        for question in db["questions"]:
            prompt, target = expected[question["id"]]
            self.assertEqual(question["prompt"], prompt)
            self.assertIn(prompt + target, corpus)
        self.assertEqual(len(db["episodes"]), 0)
        self.assertEqual(db["totals"]["episodes"], 0)

    def test_database_is_isolated_and_has_2000_parameter_policy(self):
        db = json.loads((ROOT / "data" / "reinforcement-g2" / "answers.json").read_text(encoding="utf-8"))
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
