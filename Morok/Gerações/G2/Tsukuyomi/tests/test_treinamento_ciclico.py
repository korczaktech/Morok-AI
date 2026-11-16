import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1] / "scripts"
DATA_DIR = Path(__file__).resolve().parents[1] / "data"
sys.path.insert(0, str(SCRIPT_DIR))

import treinamento_ciclico as training


class TrainingDataTests(unittest.TestCase):
    def test_learning_rate_has_no_cycle_based_decay_scheduler(self):
        self.assertFalse(hasattr(training, "decay_learning_rate"))
        self.assertFalse(hasattr(training, "LR_REDUCE_FACTOR"))
        self.assertEqual(training.INITIAL_LEARNING_RATE, 1e-3)

    def test_lexical_learning_rate_is_adaptive_for_each_question(self):
        base = 0.001
        expected = {
            0.00: 1e-3, 0.10: 9e-4, 0.20: 8e-4, 0.25: 7.5e-4,
            0.30: 7e-4, 0.40: 6e-4, 0.50: 5e-4, 0.60: 4e-4,
            0.70: 3e-4, 0.75: 2.5e-4, 0.80: 1.75e-4,
            0.85: 1e-4, 0.90: 1e-5, 1.00: 1e-5,
        }
        for level, rate in expected.items():
            self.assertAlmostEqual(training.learning_rate_for_lexical_level(base, level), rate, places=12)
        for external_lr in (1e-5, 1e-3, 1e-2):
            self.assertAlmostEqual(training.learning_rate_for_lexical_level(external_lr, 0.5), 5e-4)
        for level in (-0.1, 1.1, float("nan")):
            with self.assertRaises(ValueError):
                training.learning_rate_for_lexical_level(base, level)

    def test_global_cycle_counter_increments_and_survives_history_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "history.json"
            history = training.load_question_history(path)
            self.assertEqual(history["global_cycles"], 0)
            self.assertEqual(training.next_global_cycle(history), 1)
            self.assertEqual(training.next_global_cycle(history), 2)
            training.write_question_history(path, history)
            loaded = training.load_question_history(path)
            self.assertEqual(loaded["global_cycles"], 2)
            self.assertEqual(training.next_global_cycle(loaded), 3)

    def test_legacy_question_history_gets_global_cycle_counter(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "history.json"
            path.write_text('{"version": 1, "questions": {}}', encoding="utf-8")
            history = training.load_question_history(path)
            self.assertEqual(history["global_cycles"], 0)

    def test_checkpoint_sha256_detects_weight_and_adam_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "model.ckpt"
            self.assertIsNone(training.checkpoint_sha256(path))
            path.write_bytes(b"weights-cycle-1")
            first = training.checkpoint_sha256(path)
            self.assertEqual(len(first), 64)
            self.assertEqual(first, hashlib.sha256(b"weights-cycle-1").hexdigest())
            path.write_bytes(b"weights-cycle-2")
            second = training.checkpoint_sha256(path)
            self.assertNotEqual(first, second)

    def test_question_learning_rate_map_assigns_each_answer_byte(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            corpus = root / "training.txt"
            rate_map = root / "rates.txt"
            corpus.write_text("U: primeira?\nIA: resposta A\n\nU: segunda?\nIA: resposta B\n",
                              encoding="utf-8")
            count = training.write_question_learning_rates(
                corpus, {"primeira?": 0.001, "segunda?": 0.0001}, rate_map
            )
            self.assertEqual(count, 2)
            ranges = [line.split() for line in rate_map.read_text(encoding="ascii").splitlines()]
            self.assertEqual(len(ranges), 2)
            self.assertEqual(float(ranges[0][2]), 0.001)
            self.assertEqual(float(ranges[1][2]), 0.0001)
            raw = corpus.read_bytes()
            self.assertEqual(raw[int(ranges[0][0]):int(ranges[0][1])], b" resposta A\n")
            self.assertEqual(raw[int(ranges[1][0]):int(ranges[1][1])], b" resposta B\n")

    def test_learning_rate_bounds_are_one_e_minus_five_and_one_e_minus_two(self):
        self.assertEqual(training.MIN_LEARNING_RATE, 1e-5)
        self.assertEqual(training.MAX_LEARNING_RATE, 1e-2)
        self.assertEqual(training.learning_rate_for_lexical_level(1e-5, 1.0), 1e-5)
        self.assertEqual(training.learning_rate_for_lexical_level(1e-2, 0.0), 1e-3)
        self.assertEqual(training.learning_rate_for_lexical_level(1e-2, 1.0), 1e-5)
        self.assertEqual(training.INITIAL_LEARNING_RATE, 1e-3)

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
        self.assertEqual(len(training.read_dialogues(DATA_DIR / "feedback.txt")), 40)
        self.assertEqual(len(training.read_dialogues(DATA_DIR / "validacao.txt")), 20)
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
        self.assertEqual(training.record_feedback_errors(memory, dialogues, results), 1)
        self.assertEqual(memory, {"pergunta 1": "gabarito 1"})
        self.assertEqual(training.record_feedback_errors(memory, dialogues, results), 0)

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
            for name in ("treinamento.txt", "feedback.txt", "validacao.txt", "teste.txt")
        ]
        for index, group in enumerate(groups):
            for other in groups[index + 1:]:
                self.assertTrue(group.isdisjoint(other))


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



    def test_feedback_and_validation_are_distinct_new_datasets(self):
        feedback = training.read_dialogues(DATA_DIR / "feedback.txt")
        validation = training.read_dialogues(DATA_DIR / "validacao.txt")
        test = training.read_dialogues(DATA_DIR / "teste.txt")
        self.assertEqual(len(feedback), 40)
        self.assertEqual(len(validation), 20)
        self.assertEqual(len(test), 12)
        question_sets = [
            {question for question, _ in group}
            for group in (feedback, validation, test)
        ]
        self.assertTrue(question_sets[0].isdisjoint(question_sets[1]))
        self.assertTrue(question_sets[0].isdisjoint(question_sets[2]))
        self.assertTrue(question_sets[1].isdisjoint(question_sets[2]))
        train_questions = {
            question for question, _ in training.read_dialogues(DATA_DIR / "treinamento.txt")
        }
        self.assertTrue(train_questions.isdisjoint(question_sets[1]))

    def test_validation_count_is_limited_to_independent_holdout(self):
        self.assertEqual(training.MAX_VALIDATION_QUESTIONS, 20)
        self.assertEqual(training.MAX_FEEDBACK_QUESTIONS, 40)
        with self.assertRaises(ValueError):
            training.select_validation_questions([], [("q", "a")], 21)

    def test_checkpoint_retention_rejects_losing_previously_correct_answers(self):
        dialogues = [("q1", "a1"), ("q2", "a2"), ("q3", "a3")]
        previous = [
            ("a1", True, 1.0),
            ("errado", False, 0.0),
            ("a3", True, 1.0),
        ]
        candidate = [
            ("a1", True, 1.0),
            ("a2", True, 1.0),
            ("errado", False, 0.0),
        ]
        self.assertEqual(
            training.find_retention_regressions(previous, dialogues, candidate),
            ["q3"],
        )
        self.assertEqual(
            training.find_retention_regressions(None, dialogues, candidate), []
        )

    def test_question_history_detects_regressions_and_recoveries(self):
        history = {"version": 1, "questions": {}}
        dialogue = [("Qual é a resposta?", "Resposta correta")]
        self.assertEqual(
            training.update_question_history(
                history, dialogue, [("Resposta correta", True, 1.0)], 0, "run-a"
            ),
            (0, 0),
        )
        self.assertEqual(
            training.update_question_history(
                history, dialogue, [("Resposta errada", False, 0.2)], 1, "run-a"
            ),
            (1, 0),
        )
        self.assertEqual(
            training.update_question_history(
                history, dialogue, [("Resposta correta", True, 1.0)], 2, "run-b"
            ),
            (0, 1),
        )
        item = history["questions"]["Qual é a resposta?"]
        self.assertEqual(item["regressions"], 1)
        self.assertEqual(item["recoveries"], 1)
        self.assertTrue(item["ever_correct"])
        self.assertEqual(item["last_execution"], "run-b")
        self.assertEqual(item["dataset"], "validation")
        self.assertEqual(len(item["history"]), 3)
        self.assertEqual([entry["correct"] for entry in item["history"]], [True, False, True])

    def test_question_history_tracks_feedback_separately(self):
        history = {"version": 1, "questions": {}}
        training.update_question_history(
            history, [("pergunta de feedback", "gabarito")],
            [("resposta errada", False, 0.0)], 1, "run-1", dataset="feedback"
        )
        entry = history["questions"]["pergunta de feedback"]
        self.assertEqual(entry["dataset"], "feedback")
        self.assertEqual(entry["history"][0]["dataset"], "feedback")

    def test_question_history_round_trip_and_rejects_changed_answer_key(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "history.json"
            history = {"version": 1, "global_cycles": 0, "questions": {}}
            training.update_question_history(
                history, [("pergunta", "gabarito")],
                [("resposta", False, 0.0)], 1, "run-1"
            )
            training.write_question_history(path, history)
            loaded = training.load_question_history(path)
            self.assertEqual(loaded, history)
            with self.assertRaises(RuntimeError):
                training.update_question_history(
                    loaded, [("pergunta", "outro gabarito")],
                    [("resposta", False, 0.0)], 2, "run-2"
                )

    def test_validation_metrics_prioritize_exact_answers(self):
        dialogues = [("q1", "resposta certa"), ("q2", "outra resposta")]
        results = [
            ("resposta certa", True, 1.0),
            ("quase outra resposta", False, 0.8),
        ]
        correct, total, f1, char_score, score, rank = training.validation_metrics(
            dialogues, results
        )
        self.assertEqual((correct, total), (1, 2))
        self.assertEqual(rank[0], 1)
        self.assertTrue(0.0 <= score <= 1.0)
        self.assertTrue(0.0 <= f1 <= 1.0)
        self.assertTrue(0.0 <= char_score <= 1.0)


if __name__ == "__main__":
    unittest.main()
