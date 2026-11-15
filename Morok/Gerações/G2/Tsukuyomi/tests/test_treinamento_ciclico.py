import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1] / "scripts"
DATA_DIR = Path(__file__).resolve().parents[1] / "data"
sys.path.insert(0, str(SCRIPT_DIR))

import treinamento_ciclico as training


class TrainingDataTests(unittest.TestCase):
    def test_learning_rate_scheduler_reduces_after_plateau(self):
        lr = training.INITIAL_LEARNING_RATE
        best = 0.5
        plateau = 0
        for _ in range(training.LR_PLATEAU_CYCLES - 1):
            lr, best, plateau, reduced = training.update_learning_rate(lr, best, plateau, 0.5)
            self.assertFalse(reduced)
        lr, best, plateau, reduced = training.update_learning_rate(lr, best, plateau, 0.5)
        self.assertTrue(reduced)
        self.assertEqual(lr, training.INITIAL_LEARNING_RATE * 0.5)
        self.assertEqual(plateau, 0)

    def test_learning_rate_scheduler_resets_on_improvement(self):
        lr, best, plateau, reduced = training.update_learning_rate(
            0.001, 0.5, 4, 0.8
        )
        self.assertEqual(lr, 0.001)
        self.assertEqual(best, 0.8)
        self.assertEqual(plateau, 0)
        self.assertFalse(reduced)

    def test_continuous_training_stops_on_first_correct_single_question(self):
        self.assertTrue(training.should_stop(True, 1, 1))
        self.assertFalse(training.should_stop(True, 0, 1))
        self.assertFalse(training.should_stop(True, 1, 2))
        self.assertFalse(training.should_stop(False, 1, 1))

    def test_validation_does_not_prioritize_memorized_answers(self):
        train = [("pergunta original", "O orçamento planejado para Apollo é de 5.000 parâmetros.")]
        validation = [
            ("pergunta sem resposta aprendida", "Uma resposta ainda não treinada."),
            ("Qual é o orçamento do Apollo?", "O orçamento planejado para Apollo é de 5.000 parâmetros."),
            ("outra pergunta", "outra resposta"),
        ]
        selected = training.select_validation_questions(train, validation, 1)
        self.assertEqual(selected, [validation[0]])
    def test_split_sizes_and_original_examples_are_preserved(self):
        train = training.read_dialogues(DATA_DIR / "treinamento.txt")
        original = training.read_dialogues(DATA_DIR / "treinamento-inicial.txt")
        self.assertEqual(len(train), 56)
        self.assertEqual(len(original), 40)
        self.assertEqual(len(set(original) & set(train)), 32)
        self.assertEqual(len(training.read_dialogues(DATA_DIR / "validacao.txt")), 40)
        self.assertEqual(len(training.read_dialogues(DATA_DIR / "teste.txt")), 12)

    def test_training_metrics_are_parsed_and_must_be_finite(self):
        output = (
            "Épocas: 1\\nBytes-alvo: 512\\n"
            "train_bpb inicial: 7.5\\ntrain_bpb final: 6.9\\n"
        )
        self.assertEqual(training.extract_training_metric(output, "Épocas"), 1.0)
        self.assertEqual(training.extract_training_metric(output, "Bytes-alvo"), 512.0)
        self.assertAlmostEqual(training.extract_training_metric(output, "train_bpb final"), 6.9)
        with self.assertRaises(RuntimeError):
            training.extract_training_metric("train_bpb final: nan\\n", "train_bpb final")
        with self.assertRaises(RuntimeError):
            training.extract_training_metric("", "train_bpb final")

    def test_best_checkpoint_and_adam_state_are_saved_and_restored_together(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkpoint = root / "model.ckpt"
            optimizer = root / "model.ckpt.adam"
            best_checkpoint = root / "model.ckpt.best"
            best_optimizer = root / "model.ckpt.adam.best"
            checkpoint.write_bytes(b"weights-cycle-3")
            optimizer.write_bytes(b"adam-cycle-3")
            training.save_best_state(checkpoint, optimizer, best_checkpoint, best_optimizer)
            checkpoint.write_bytes(b"weights-cycle-4")
            optimizer.write_bytes(b"adam-cycle-4")
            training.restore_best_state(best_checkpoint, best_optimizer, checkpoint, optimizer)
            self.assertEqual(checkpoint.read_bytes(), b"weights-cycle-3")
            self.assertEqual(optimizer.read_bytes(), b"adam-cycle-3")

    def test_best_state_requires_both_checkpoint_and_optimizer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkpoint = root / "model.ckpt"
            optimizer = root / "model.ckpt.adam"
            checkpoint.write_bytes(b"weights")
            with self.assertRaises(RuntimeError):
                training.save_best_state(
                    checkpoint, optimizer, root / "best.ckpt", root / "best.adam"
                )

    def test_report_separates_correct_and_incorrect_answers(self):
        from io import StringIO

        report = StringIO()
        dialogues = [("pergunta correta?", "resposta certa"),
                     ("pergunta incorreta?", "resposta esperada")]
        results = [("resposta certa", True, 1.0),
                   ("resposta errada", False, 0.0)]

        correct, total = training.write_results(report, "Validação", dialogues, results)
        content = report.getvalue()

        self.assertEqual((correct, total), (1, 2))
        self.assertIn("Respostas corretas (1)", content)
        self.assertIn("Respostas incorretas (1)", content)
        self.assertIn("Resposta gerada: resposta certa", content)
        self.assertIn("Resposta gerada: resposta errada", content)
        self.assertIn("Gabarito: resposta esperada", content)

    def test_splits_have_no_question_overlap(self):
        groups = [
            {question for question, _ in training.read_dialogues(DATA_DIR / name)}
            for name in ("treinamento.txt", "validacao.txt", "teste.txt")
        ]
        self.assertTrue(groups[0].isdisjoint(groups[1]))
        self.assertTrue(groups[0].isdisjoint(groups[2]))
        self.assertTrue(groups[1].isdisjoint(groups[2]))


    def test_bom_and_indented_markers(self):
        with tempfile.TemporaryDirectory() as directory:
            corpus = Path(directory) / "dialogue.txt"
            corpus.write_bytes(
                b"\xef\xbb\xbf  U: pergunta\r\n  IA: resposta\r\n"
            )
            self.assertEqual(training.read_dialogues(corpus), [("pergunta", "resposta")])

    def test_generation_diagnostics_detect_repetition_and_diversity(self):
        repetition, diversity = training.generation_diagnostics("aaaa")
        self.assertEqual(repetition, 1.0)
        self.assertEqual(diversity, 0.25)
        repetition, diversity = training.generation_diagnostics("abcd")
        self.assertEqual(repetition, 0.25)
        self.assertEqual(diversity, 1.0)

    def test_exact_answer_scores_one(self):
        correct, score = training.evaluate("Sim, o céu é azul.", "Sim, o céu é azul.")
        self.assertTrue(correct)
        self.assertEqual(score, 1.0)

    def test_near_match_does_not_pass_as_correct(self):
        correct, score = training.evaluate(
            "Sim, o céu é azul e verde.", "Sim, o céu é azul."
        )
        self.assertFalse(correct)
        self.assertGreater(score, 0.0)

    def test_accent_and_punctuation_normalization(self):
        correct, score = training.evaluate("O ceu e azul!", "O céu é azul.")
        self.assertTrue(correct)
        self.assertEqual(score, 1.0)

    def test_repeated_words_are_counted(self):
        correct, score = training.evaluate("azul azul", "azul")
        self.assertFalse(correct)
        self.assertLess(score, 0.90)

    def test_empty_answer_fails(self):
        correct, score = training.evaluate("", "uma resposta")
        self.assertFalse(correct)
        self.assertEqual(score, 0.0)


if __name__ == "__main__":
    unittest.main()
