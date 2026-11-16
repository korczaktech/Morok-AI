import hashlib
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1] / "scripts"
DATA_DIR = Path(__file__).resolve().parents[1] / "data"
sys.path.insert(0, str(SCRIPT_DIR))

import treinamento_ciclico as training


class TrainingDataTests(unittest.TestCase):
    def test_only_the_100_training_questions_exist_in_the_training_flow(self):
        dialogues = training.read_dialogues(DATA_DIR / "feedback.txt")
        self.assertEqual(training.TRAINING_QUESTIONS, 100)
        self.assertEqual(len(dialogues), 100)
        self.assertEqual(len({question.casefold() for question, _ in dialogues}), 100)
        self.assertEqual(len({answer.casefold() for _, answer in dialogues}), 100)
        self.assertFalse(hasattr(training, "MAX_VALIDATION_QUESTIONS"))
        self.assertFalse(hasattr(training, "evaluate_validation_loss"))
        self.assertFalse(hasattr(training, "improves_validation_loss"))

    def test_dialogue_reader_rejects_duplicate_questions_and_answers(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "duplicate-questions.txt"
            path.write_text(
                "U: mesma pergunta?\\nIA: resposta um\\n\\n"
                "U: MESMA pergunta!\\nIA: resposta dois\\n",
                encoding="utf-8",
            )
            with self.assertRaises(RuntimeError):
                training.read_dialogues(path)
            path.write_text(
                "U: pergunta um?\\nIA: mesma resposta\\n\\n"
                "U: pergunta dois?\\nIA: MESMA resposta!\\n",
                encoding="utf-8",
            )
            with self.assertRaises(RuntimeError):
                training.read_dialogues(path)

    def test_learning_rate_is_adaptive_per_question(self):
        expected = {
            0.00: 1e-3, 0.10: 9e-4, 0.20: 8e-4, 0.25: 7.5e-4,
            0.30: 7e-4, 0.40: 6e-4, 0.50: 5e-4, 0.60: 4e-4,
            0.70: 3e-4, 0.75: 2.5e-4, 0.80: 1.75e-4,
            0.85: 1e-4, 0.90: 1e-5, 1.00: 1e-5,
        }
        for level, rate in expected.items():
            self.assertAlmostEqual(training.learning_rate_for_lexical_level(0.001, level), rate, places=12)
        self.assertEqual(training.MIN_LEARNING_RATE, 1e-5)
        self.assertEqual(training.MAX_LEARNING_RATE, 1e-2)
        self.assertEqual(training.INITIAL_LEARNING_RATE, 1e-3)

    def test_no_global_cycle_learning_rate_scheduler(self):
        self.assertFalse(hasattr(training, "decay_learning_rate"))
        self.assertFalse(hasattr(training, "LR_REDUCE_FACTOR"))

    def test_training_budget_is_unchanged(self):
        self.assertEqual(training.MAX_CYCLES, 100)
        self.assertEqual(training.MAX_EPOCHS_PER_CYCLE, 1000)

    def test_training_corpus_keeps_exactly_100_questions_and_overrides_answers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base.txt"
            combined = root / "combined.txt"
            base.write_text(
                "U: primeira?\nIA: resposta original 1\n\n"
                "U: segunda?\nIA: resposta original 2\n",
                encoding="utf-8",
            )
            training.build_training_corpus(base, {"primeira?": "resposta corrigida"}, combined)
            self.assertEqual(
                training.read_dialogues(combined),
                [("primeira?", "resposta corrigida"), ("segunda?", "resposta original 2")],
            )

    def test_training_corpus_rejects_questions_outside_the_100(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base.txt"
            base.write_text("U: pergunta\nIA: resposta\n", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                training.build_training_corpus(base, {"outra": "correção"}, root / "out.txt")

    def test_individual_loss_corpus_uses_real_line_breaks(self):
        with tempfile.TemporaryDirectory() as directory:
            corpus = Path(directory) / "single-question.txt"
            training.write_evaluation_corpus(corpus, "pergunta?", "resposta esperada")
            self.assertEqual(
                corpus.read_bytes(),
                b"U: pergunta?\nIA: resposta esperada\n",
            )
            self.assertNotIn(b"\\n", corpus.read_bytes())
            self.assertEqual(
                training.read_dialogues(corpus),
                [("pergunta?", "resposta esperada")],
            )

    def test_learning_rate_map_assigns_each_answer_byte(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            corpus = root / "training.txt"
            rate_map = root / "rates.txt"
            corpus.write_text("U: primeira?\nIA: resposta A\n\nU: segunda?\nIA: resposta B\n", encoding="utf-8")
            count = training.write_question_learning_rates(corpus, {"primeira?": 0.001, "segunda?": 0.0001}, rate_map)
            self.assertEqual(count, 2)
            ranges = [line.split() for line in rate_map.read_text(encoding="ascii").splitlines()]
            raw = corpus.read_bytes()
            self.assertEqual(raw[int(ranges[0][0]):int(ranges[0][1])], b" resposta A\n")
            self.assertEqual(raw[int(ranges[1][0]):int(ranges[1][1])], b" resposta B\n")

    def test_feedback_memory_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "memory.txt"
            memory = {"pergunta": "resposta corrigida"}
            training.write_feedback_memory(path, memory)
            self.assertEqual(training.load_feedback_memory(path), memory)

    def test_incorrect_training_answers_enter_deduplicated_memory(self):
        memory = {}
        dialogues = [("pergunta 1", "gabarito 1"), ("pergunta 2", "gabarito 2")]
        results = [("errada", False, 0.2), ("gabarito 2", True, 1.0)]
        self.assertEqual(training.record_feedback_errors(memory, dialogues, results), 1)
        self.assertEqual(memory, {"pergunta 1": "gabarito 1"})
        self.assertEqual(training.record_feedback_errors(memory, dialogues, results), 0)

    def test_global_cycle_counter_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "history.json"
            history = training.load_question_history(path)
            self.assertEqual(training.next_global_cycle(history), 1)
            training.write_question_history(path, history)
            loaded = training.load_question_history(path)
            self.assertEqual(training.next_global_cycle(loaded), 2)

    def test_each_question_has_an_independent_checkpoint_and_adam_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = training.question_checkpoint_paths(root, "pergunta 1?")
            second = training.question_checkpoint_paths(root, "pergunta 2?")
            self.assertNotEqual(first[0], second[0])
            self.assertTrue(first[0].name.startswith("q_"))
            self.assertEqual(first[1].name, first[0].name + ".adam")

    def test_loss_acceptance_is_independent_for_each_question(self):
        # A questão 1 piora e é rejeitada; a questão 2 melhora e é aceita.
        self.assertFalse(training.accept_question_checkpoint(10.0, 11.0))
        self.assertTrue(training.accept_question_checkpoint(20.0, 19.0))
        self.assertTrue(training.accept_question_checkpoint(None, 30.0))
        with self.assertRaises(ValueError):
            training.accept_question_checkpoint(10.0, float("nan"))

    def test_checkpoint_hash_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "model.ckpt"
            self.assertIsNone(training.checkpoint_sha256(path))
            path.write_bytes(b"weights-a")
            first = training.checkpoint_sha256(path)
            self.assertEqual(first, hashlib.sha256(b"weights-a").hexdigest())
            path.write_bytes(b"weights-b")
            self.assertNotEqual(first, training.checkpoint_sha256(path))

    def test_checkpoint_and_adam_are_saved_and_restored_together(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkpoint, adam = root / "model.ckpt", root / "model.ckpt.adam"
            best, best_adam = root / "best.ckpt", root / "best.adam"
            checkpoint.write_bytes(b"weights-1")
            adam.write_bytes(b"adam-1")
            training.save_best_state(checkpoint, adam, best, best_adam)
            checkpoint.write_bytes(b"weights-2")
            adam.write_bytes(b"adam-2")
            training.restore_best_state(best, best_adam, checkpoint, adam)
            self.assertEqual(checkpoint.read_bytes(), b"weights-1")
            self.assertEqual(adam.read_bytes(), b"adam-1")

    def test_resume_requires_checkpoint_and_adam_pair(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkpoint, adam = root / "model.ckpt", root / "model.ckpt.adam"
            self.assertFalse(training.resumable_state_exists(checkpoint, adam))
            checkpoint.write_bytes(b"weights")
            with self.assertRaises(RuntimeError):
                training.resumable_state_exists(checkpoint, adam)
            adam.write_bytes(b"adam")
            self.assertTrue(training.resumable_state_exists(checkpoint, adam))

    def test_continuous_training_stops_only_when_all_100_are_correct(self):
        self.assertTrue(training.should_stop(True, 100, 100))
        self.assertFalse(training.should_stop(True, 99, 100))
        self.assertFalse(training.should_stop(False, 100, 100))

    def test_metrics_are_parsed_and_finite(self):
        output = "Épocas: 1\nBytes-alvo: 512\ntrain_bpb inicial: 7.5\ntrain_bpb final: 6.9\nLoss inicial: 5.0\nLoss final: 4.0\n"
        self.assertEqual(training.extract_training_metric(output, "Épocas"), 1.0)
        self.assertAlmostEqual(training.extract_training_metric(output, "Loss final"), 4.0)
        with self.assertRaises(RuntimeError):
            training.extract_training_metric("Loss final: nan\n", "Loss final")

    def test_history_tracks_training_dataset(self):
        history = {"version": 1, "global_cycles": 0, "questions": {}}
        dialogues = [("pergunta", "resposta")]
        result = [("resposta", True, 1.0)]
        training.update_question_history(history, dialogues, result, 1, "run-1", dataset="training")
        self.assertEqual(history["questions"]["pergunta"]["dataset"], "training")

    def test_history_keeps_a_separate_best_loss_for_each_question(self):
        history = {"version": 1, "global_cycles": 0, "questions": {}}
        dialogues = [("pergunta 1", "resposta 1"), ("pergunta 2", "resposta 2")]
        first = [
            ("r1", False, 0.2, 10.0, 14.4, 8),
            ("r2", False, 0.3, 20.0, 28.8, 8),
        ]
        training.update_question_history(history, dialogues, first, 1, "run-1")
        second = [
            ("r1", False, 0.4, 11.0, 15.8, 8),
            ("r2", True, 1.0, 19.0, 27.4, 8),
        ]
        training.update_question_history(history, dialogues, second, 2, "run-2")
        self.assertEqual(history["questions"]["pergunta 1"]["best_loss"], 10.0)
        self.assertEqual(history["questions"]["pergunta 2"]["best_loss"], 19.0)
        self.assertEqual(history["questions"]["pergunta 1"]["last_loss"], 11.0)
        self.assertEqual(history["questions"]["pergunta 2"]["last_loss"], 19.0)

    def test_each_question_report_contains_individual_metrics_and_colored_status(self):
        dialogues = [
            ("pergunta correta?", "resposta certa"),
            ("pergunta incorreta?", "gabarito esperado"),
        ]
        results = [
            ("resposta certa", True, 1.0, 0.12, 0.1731, 14),
            ("resposta errada", False, 0.5, 1.20, 1.7312, 18),
        ]
        report = io.StringIO()
        logs = io.StringIO()
        with redirect_stdout(logs):
            correct, total = training.write_results(report, "Ciclo 1", dialogues, results)
        self.assertEqual((correct, total), (1, 2))
        output = report.getvalue()
        self.assertIn('color: green', output)
        self.assertIn('Success: Pergunta 01 — pergunta correta?', output)
        self.assertIn('color: red', output)
        self.assertIn('Error: Pergunta 02 — pergunta incorreta?', output)
        self.assertIn('Resposta gerada: resposta certa', output)
        self.assertIn('Nível lexical (F1): 100.00%', output)
        self.assertIn('Loss individual: 0.12000000', output)
        self.assertIn('BPB individual: 0.17310000', output)
        self.assertIn('Learning rate individual:', output)
        self.assertIn('Bytes-alvo da resposta: 18', output)
        self.assertIn(chr(27) + '[32mSuccess: Pergunta 01', logs.getvalue())
        self.assertIn(chr(27) + '[31mError: Pergunta 02', logs.getvalue())

    def test_exact_match_normalization_and_diagnostics(self):
        correct, score = training.evaluate("O ceu e azul!", "O céu é azul.")
        self.assertTrue(correct)
        self.assertEqual(score, 1.0)
        self.assertEqual(training.character_similarity("O céu é azul!", "O ceu e azul."), 1.0)
        repetition, diversity = training.generation_diagnostics("aaaa")
        self.assertEqual(repetition, 1.0)
        self.assertEqual(diversity, 0.25)


if __name__ == "__main__":
    unittest.main()
