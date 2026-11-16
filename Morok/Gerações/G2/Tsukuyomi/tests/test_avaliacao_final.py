import importlib.util
from pathlib import Path
import unittest

SCRIPT = Path(__file__).parents[1] / "scripts" / "avaliacao_final.py"
SPEC = importlib.util.spec_from_file_location("avaliacao_final", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class FinalEvaluationTests(unittest.TestCase):
    def test_normalized_exact_match_ignores_case_accents_and_punctuation(self):
        result = MODULE.metrics("AÇÃO, rápida!", "acao rapida")
        self.assertTrue(result["exact"])
        self.assertEqual(result["lexical_f1"], 1.0)
        self.assertEqual(result["character_similarity"], 1.0)

    def test_semantically_related_but_lexically_different_is_not_exact(self):
        result = MODULE.metrics("Não posso controlar o aparelho.", "Não tenho controle do celular.")
        self.assertFalse(result["exact"])
        self.assertGreaterEqual(result["lexical_f1"], 0.0)
        self.assertLessEqual(result["lexical_f1"], 1.0)

    def test_empty_answer_is_not_exact_and_metrics_are_finite(self):
        result = MODULE.metrics("", "uma resposta")
        self.assertFalse(result["exact"])
        for value in result.values():
            if isinstance(value, float):
                self.assertTrue(__import__("math").isfinite(value))

    def test_repetition_and_diversity_are_bounded(self):
        result = MODULE.metrics("aaaa", "abcd")
        self.assertGreaterEqual(result["consecutive_repetition"], 0.0)
        self.assertLessEqual(result["consecutive_repetition"], 1.0)
        self.assertGreaterEqual(result["character_diversity"], 0.0)
        self.assertLessEqual(result["character_diversity"], 1.0)

    def test_original_training_examples_are_preserved_and_splits_do_not_leak(self):
        data = SCRIPT.parents[1] / "data"
        original = set(MODULE.read_dialogues(data / "treinamento-inicial.txt"))
        training = set(MODULE.read_dialogues(data / "treinamento.txt"))
        feedback = set(MODULE.read_dialogues(data / "feedback.txt"))
        validation = set(MODULE.read_dialogues(data / "validacao.txt"))
        test = set(MODULE.read_dialogues(data / "teste.txt"))
        self.assertEqual(len(training), 56)
        self.assertEqual(len(original & training), 32)
        self.assertFalse(training & feedback)
        self.assertFalse(training & validation)
        self.assertFalse(training & test)
        self.assertFalse(feedback & validation)
        self.assertFalse(feedback & test)
        self.assertFalse(validation & test)
        train_questions = {question for question, _ in training}
        feedback_questions = {question for question, _ in feedback}
        validation_questions = {question for question, _ in validation}
        test_questions = {question for question, _ in test}
        self.assertFalse(train_questions & feedback_questions)
        self.assertFalse(train_questions & validation_questions)
        self.assertFalse(train_questions & test_questions)
        self.assertFalse(feedback_questions & validation_questions)
        self.assertFalse(feedback_questions & test_questions)
        self.assertFalse(validation_questions & test_questions)

    def test_corpora_keep_validation_and_test_sizes(self):
        data = SCRIPT.parents[1] / "data"
        self.assertEqual(len(MODULE.read_dialogues(data / "feedback.txt")), 40)
        self.assertEqual(len(MODULE.read_dialogues(data / "validacao.txt")), 20)
        self.assertEqual(len(MODULE.read_dialogues(data / "teste.txt")), 12)


    def test_benchmark_categories_cover_all_skills(self):
        self.assertEqual(MODULE.benchmark_category("Compare Apollo e Mokosh"), "composicao")
        self.assertEqual(MODULE.benchmark_category("Explique o contexto"), "conceito")
        self.assertEqual(MODULE.benchmark_category("O que é contexto?"), "conceito")
        self.assertEqual(MODULE.benchmark_category("Quantos parâmetros existem?"), "fato")
        self.assertEqual(MODULE.benchmark_category("Olá, tudo bem?"), "conversa")

    def test_final_report_summarizes_each_skill(self):
        dialogues = [
            ("Compare Apollo e Mokosh", "Apollo é equilibrado; Mokosh é rápido."),
            ("Explique o contexto", "Contexto ajuda na previsão."),
            ("Quantos parâmetros existem?", "São dez mil parâmetros."),
            ("Olá, tudo bem?", "Olá."),
        ]
        answers = [answer for _, answer in dialogues]
        summary = MODULE.summarize_rows(dialogues, answers)
        self.assertEqual(set(summary), {"composicao", "conceito", "fato", "conversa"})
        self.assertTrue(all(item["questions"] == 1 for item in summary.values()))
        self.assertTrue(all(item["exact_matches"] == 1 for item in summary.values()))

    def test_skill_summary_rejects_mismatched_inputs(self):
        with self.assertRaises(ValueError):
            MODULE.summarize_rows([("pergunta", "resposta")], [])
        with self.assertRaises(ValueError):
            MODULE.summarize_rows([], [])


    def test_tfidf_retrieval_baseline_returns_exact_training_answer(self):
        training = [
            ("Qual é seu nome?", "Meu nome é Morok."),
            ("Como funciona o contexto?", "Contexto ajuda na previsão."),
        ]
        questions = [("Qual é seu nome?", "Meu nome é Morok.")]
        self.assertEqual(
            MODULE.retrieval_baseline_answers(training, questions),
            ["Meu nome é Morok."],
        )
        result = MODULE.evaluate_retrieval_baseline(training, questions)
        self.assertEqual(result["exact_matches"], 1)
        self.assertEqual(result["exact_match_rate"], 1.0)

    def test_retrieval_baseline_rejects_empty_training_and_evaluation(self):
        with self.assertRaises(ValueError):
            MODULE.retrieval_baseline_answers([], [("pergunta", "resposta")])
        with self.assertRaises(ValueError):
            MODULE.evaluate_retrieval_baseline(
                [("pergunta", "resposta")], []
            )


if __name__ == "__main__":
    unittest.main()
