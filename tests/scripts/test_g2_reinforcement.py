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
build_memory_replay = MODULE["build_memory_replay"]
reconcile_episode_outcomes = MODULE["reconcile_episode_outcomes"]
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

    def test_rejects_correct_prefix_followed_by_garbage(self):
        self.assertEqual(grade("sky_is_blue", "Sim, o ceu e azul*garbage text")[0:2],
                         ("incorrect", -1))
        self.assertEqual(grade("grass_color", "A grama e verde. texto aleatorio")[0:2],
                         ("incorrect", -1))
        self.assertEqual(grade("mountains_high_or_low", "As montanhas sao altas. As montanhas sao altas.")[0:2],
                         ("incorrect", -1))

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

    def test_memory_replays_persisted_model_answers_and_oracle_corrections(self):
        episodes = [
            {"episode_id": "1", "question_id": "sky_is_blue",
             "prompt": "Usuario: O ceu e azul?\nIA: ",
             "initial_answer": "Sim, o ceu e azul", "answer": "Sim, o ceu e azul!",
             "policy_update": {"status": "applied", "source": "model_response"}},
            {"episode_id": "2", "question_id": "grass_color",
             "prompt": "Usuario: Que cor e a grama?\nIA: ",
             "initial_answer": "grama roxa", "answer": "A grama e verde.",
             "policy_update": {"status": "applied", "source": "oracle_demonstration"}},
            {"episode_id": "3", "question_id": "mountains_high_or_low",
             "prompt": "prompt", "answer": "incompleta",
             "policy_update": {"status": "skipped"}},
        ]
        replay = build_memory_replay(episodes, 32)
        self.assertEqual(len(replay), 2)
        self.assertEqual(replay[0]["target"], "Sim, o ceu e azul")
        self.assertEqual(replay[0]["source"], "model_response")
        self.assertEqual(replay[1]["target"], SEED_ANSWERS["grass_color"])
        self.assertEqual(replay[1]["source"], "oracle_demonstration")
        self.assertEqual(build_memory_replay(episodes, 1)[0]["episode_id"], "2")
        self.assertEqual(build_memory_replay(episodes, 0), [])
        with self.assertRaises(ValueError):
            build_memory_replay(episodes, 257)


    def test_memory_replay_replaces_historical_noisy_model_targets_with_oracle(self):
        episodes = [{
            "episode_id": "legacy-noisy",
            "question_id": "sky_is_blue",
            "prompt": "Usuario: O ceu e azul?\\nIA: ",
            "initial_answer": "Sim, o ceu e azul*garbage",
            "answer": "Sim, o ceu e azul*more-garbage",
            "policy_update": {"status": "applied", "source": "model_response"},
        }]
        replay = build_memory_replay(episodes, 32)
        self.assertEqual(len(replay), 1)
        self.assertEqual(replay[0]["target"], SEED_ANSWERS["sky_is_blue"])
        self.assertEqual(replay[0]["source"], "oracle_demonstration")

    def test_reconciles_legacy_totals_against_strict_grader(self):
        db = {
            "episodes": [{
                "question_id": "sky_is_blue",
                "answer": "Sim, o ceu e azul*garbage",
                "policy_update": {"status": "applied", "source": "model_response"},
            }],
            "totals": {"episodes": 1, "correct": 1, "incorrect": 0,
                       "policy_updates": 1, "skipped_updates": 0,
                       "oracle_demonstrations": 0},
        }
        reconcile_episode_outcomes(db)
        self.assertEqual(db["episodes"][0]["verdict"], "incorrect")
        self.assertEqual(db["episodes"][0]["reward"], -1)
        self.assertEqual(db["totals"]["correct"], 0)
        self.assertEqual(db["totals"]["incorrect"], 1)
        self.assertEqual(db["totals"]["policy_updates"], 1)

    def test_runner_replays_memory_before_new_attempts(self):
        runner = (ROOT / "scripts" / "run-g2-reinforcement.py").read_text(encoding="utf-8")
        self.assertIn('default=32,help="number of latest persisted experiences', runner)
        self.assertIn('memory=build_memory_replay(db["episodes"],a.memory_replay_limit)', runner)
        self.assertIn('a.memory_replay_steps', runner)
        self.assertIn('"experiences_considered":len(memory)', runner)


    def test_runner_uses_adaptive_training_and_reports_sequence_loss(self):
        runner = (ROOT / "scripts" / "run-g2-reinforcement.py").read_text(encoding="utf-8")
        app = (ROOT / "apps" / "reinforce_g2.cpp").read_text(encoding="utf-8")
        self.assertIn('default=10000)', runner)
        self.assertIn('MAX_BOOTSTRAP_EPOCHS=10000', runner)
        self.assertIn('bootstrap_epochs<=MAX_BOOTSTRAP_EPOCHS', runner)
        self.assertIn('default=.001)', runner)
        self.assertIn('str(a.learning_rate),"1000"', runner)
        self.assertIn('optimizer_steps":1000', runner)
        self.assertIn('episode["initial_answer"]=response', runner)
        self.assertIn('run([str(a.generator),str(checkpoint),str(prompt),str(answer),"48"])', runner)
        self.assertIn('previous_verdict=verdict', runner)
        self.assertIn('"--patience",str(a.bootstrap_epochs)', runner)
        workflow = (ROOT / ".github" / "workflows" / "reinforcement-g2.yml").read_text(encoding="utf-8")
        self.assertIn('inputs.epochs || 10000', workflow)
        self.assertIn('gh run download "$previous_run"', workflow)
        self.assertIn('model.v3.expert-a', workflow)
        self.assertIn('model.v3.expert-b', workflow)
        self.assertIn("if-no-files-found: warn", workflow)
        self.assertIn('target_loss_before', app)
        self.assertIn('target_loss_after', app)
        self.assertIn('G1OptimizerConfig::adamw()', app)

    def test_runner_isolates_new_checkpoints_and_requires_loss_progress(self):
        runner = (ROOT / "scripts" / "run-g2-reinforcement.py").read_text(encoding="utf-8")
        app = (ROOT / "apps" / "reinforce_g2.cpp").read_text(encoding="utf-8")
        self.assertIn('checkpoint=Path(str(a.checkpoint)+".v3")', runner)
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

    def test_g2_corpus_uses_independent_documents_for_each_eos_boundary(self):
        fixture_dir = ROOT / "tests" / "fixtures" / "g2-single-document"
        documents = sorted(fixture_dir.glob("*.txt"))
        self.assertEqual(len(documents), 3)
        expected = {
            "sky_is_blue": ("Usuario: O ceu e azul?\nIA: ", "Sim, o ceu e azul"),
            "mountains_high_or_low": ("Usuario: As montanhas sao altas ou baixas?\nIA: ", "As montanhas sao altas."),
            "grass_color": ("Usuario: Que cor e a grama?\nIA: ", "A grama e verde."),
        }
        corpus = [path.read_text(encoding="utf-8") for path in documents]
        db = json.loads((ROOT / "data" / "reinforcement-g2" / "answers.json").read_text(encoding="utf-8"))
        for question in db["questions"]:
            prompt, target = expected[question["id"]]
            self.assertEqual(question["prompt"], prompt)
            self.assertIn(prompt + target, corpus)
        self.assertTrue(all(not document.endswith("\n") for document in corpus))
        trainer = (ROOT / "src" / "model" / "learn" / "g2_training.cpp").read_text(encoding="utf-8")
        self.assertEqual(trainer.count("train_all_documents=true;"), 2)
        self.assertEqual(db["totals"]["episodes"], len(db["episodes"]))

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
