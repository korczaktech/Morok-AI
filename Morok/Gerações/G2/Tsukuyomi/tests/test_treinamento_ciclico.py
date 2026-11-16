import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1] / "scripts"
DATA_DIR = Path(__file__).resolve().parents[1] / "data"
sys.path.insert(0, str(SCRIPT_DIR))

import treinamento_ciclico as training


class TrainingDataTests(unittest.TestCase):
    def test_learning_rate_drops_ten_percent_after_every_cycle(self):
        lr = training.INITIAL_LEARNING_RATE
        expected = lr
        for _ in range(5):
            expected *= 0.9
            lr = training.decay_learning_rate(lr)
            self.assertAlmostEqual(lr, expected, places=12)

    def test_learning_rate_floor_is_one_e_minus_seven(self):
        self.assertEqual(training.MIN_LEARNING_RATE, 1e-7)
        self.assertEqual(training.decay_learning_rate(1.01e-7), 1e-7)

    def test_learning_rate_rejects_values_below_floor_or_non_finite(self):
        for value in (0.0, 0.5e-7, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                training.decay_learning_rate(value)

    def test_training_budget_is_finite_and_matches_default_plan(self):
        self.assertEqual(training.MAX_CYCLES, 100)
        self.assertEqual(training.MAX_EPOCHS_PER_CYCLE, 1000)
        self.assertEqual(1000 * 100, 100000)
        self.assertLessEqual(1000, training.MAX_EPOCHS_PER_CYCLE)
        self.assertLessEqual(100, training.MAX_CYCLES)

    def test_continuous_training_stops_on_first_correct_single_question(self):
        self.assertTrue(training.should_stop(True, 1, 1))
        self.assertFalse(training.should_stop(True, 0, 1))
        self.assertFalse(training.should_stop(True, 1, 2))
        self.assertFalse(training.should_stop(False, 1, 1))

    def test_validation_selection_preserves_fixed_order(self):
        train = [("pergunta original", "O orçamento planejado para Apollo é de 5.000 parâmetros.")]
        validation = [
            ("pergunta sem resposta aprendida", "Uma resposta ainda não treinada."),
            ("Qual é o orçamento do Apollo?", "O orçamento planejado para Apollo é de 5.000 parâmetros."),
            ("outra pergunta", "outra resposta"),
        ]
        selected = training.select_validation_questions(train, validation, 1)
        self.assertEqual(selected, [validation[0]])

    def test_reserved_test_answers_are_unseen_in_training(self):
        train = training.read_dialogues(DATA_DIR / "treinamento.txt")
        test = training.read_dialogues(DATA_DIR / "teste.txt")
        train_answers = {answer for _, answer in train}
        train_questions = {question for question, _ in train}
        self.assertFalse(train_questions & {question for question, _ in test})
        self.assertFalse(train_answers & {answer for _, answer in test})
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
            "Épocas: 1\nBytes-alvo: 512\n"
            "train_bpb inicial: 7.5\ntrain_bpb final: 6.9\n"
        )
        self.assertEqual(training.extract_training_metric(output, "Épocas"), 1.0)
        self.assertEqual(training.extract_training_metric(output, "Bytes-alvo"), 512.0)
        self.assertAlmostEqual(training.extract_training_metric(output, "train_bpb final"), 6.9)
        with self.assertRaises(RuntimeError):
            training.extract_training_metric("train_bpb final: nan\n", "train_bpb final")
        with self.assertRaises(RuntimeError):
            training.extract_training_metric("", "train_bpb final")

    def test_incorrect_validation_answers_enter_deduplicated_feedback_memory(self):
        memory = {}
        dialogues = [
            ("pergunta 1", "gabarito 1"),
            ("pergunta 2", "gabarito 2"),
        ]
        results = [
            ("resposta errada", False, 0.2),
            ("gabarito 2", True, 1.0),
        ]
        self.assertEqual(training.record_validation_errors(memory, dialogues, results), 1)
        self.assertEqual(memory, {"pergunta 1": "gabarito 1"})
        self.assertEqual(training.record_validation_errors(memory, dialogues, results), 0)

    def test_feedback_memory_round_trip_and_combined_corpus_preserve_original(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base.txt"
            memory_file = root / "feedback.txt"
            combined = root / "combined.txt"
            base.write_text("U: original\nIA: exemplo\n", encoding="utf-8")
            memory = {"pergunta errada": "resposta corrigida"}
            training.write_feedback_memory(memory_file, memory)
            self.assertEqual(training.load_feedback_memory(memory_file), memory)
            training.build_training_corpus(base, memory, combined)
            dialogues = training.read_dialogues(combined)
            self.assertIn(("original", "exemplo"), dialogues)
            self.assertIn(("pergunta errada", "resposta corrigida"), dialogues)
            self.assertEqual(base.read_text(encoding="utf-8"), "U: original\nIA: exemplo\n")

    def test_persistent_resume_requires_checkpoint_and_adam_pair(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkpoint = root / "model.ckpt"
            optimizer = root / "model.ckpt.adam"
            self.assertFalse(training.resumable_state_exists(checkpoint, optimizer))
            checkpoint.write_bytes(b"weights")
            with self.assertRaises(RuntimeError):
                training.resumable_state_exists(checkpoint, optimizer)
            optimizer.write_bytes(b"adam")
            self.assertTrue(training.resumable_state_exists(checkpoint, optimizer))
            optimizer.write_bytes(b"")
            with self.assertRaises(RuntimeError):
                training.resumable_state_exists(checkpoint, optimizer)

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

    def test_character_similarity_is_bounded_and_normalized(self):
        self.assertEqual(training.character_similarity("O céu é azul!", "O ceu e azul."), 1.0)
        self.assertGreater(training.character_similarity("o céu azul", "o céu é azul"), 0.0)
        self.assertLessEqual(training.character_similarity("resposta", "pergunta"), 1.0)
        self.assertEqual(training.character_similarity("", "resposta"), 0.0)

    def test_benchmark_categories_cover_composition_concepts_and_facts(self):
        self.assertEqual(training.benchmark_category("Compare Apollo e Mokosh"), "composicao")
        self.assertEqual(training.benchmark_category("Explique o que é contexto"), "conceito")
        self.assertEqual(training.benchmark_category("O que é contexto?"), "conceito")
        self.assertEqual(training.benchmark_category("Quantos parâmetros existem?"), "fato")
        self.assertEqual(training.benchmark_category("Olá, tudo bem?"), "conversa")

    def test_report_includes_metrics_by_skill(self):
        from io import StringIO
        report = StringIO()
        dialogues = [
            ("Compare dois modelos", "Apollo é equilibrado e Mokosh é rápido."),
            ("Explique contexto", "Contexto ajuda a prever o próximo byte."),
        ]
        results = [
            ("Apollo é equilibrado e Mokosh é rápido.", True, 1.0),
            ("Contexto ajuda.", False, 0.5),
        ]
        training.write_results(report, "Benchmark", dialogues, results)
        content = report.getvalue()
        self.assertIn("Resultado por habilidade", content)
        self.assertIn("composicao", content)
        self.assertIn("conceito", content)
        self.assertIn("Similaridade de caracteres média", content)
        self.assertIn("Aprovação exige igualdade lexical normalizada", content)

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
