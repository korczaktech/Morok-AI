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
        validation = set(MODULE.read_dialogues(data / "validacao.txt"))
        test = set(MODULE.read_dialogues(data / "teste.txt"))
        self.assertEqual(len(training), 56)
        self.assertEqual(len(original & training), 32)
        self.assertFalse(training & validation)
        self.assertFalse(training & test)
        self.assertFalse(validation & test)

    def test_corpora_keep_validation_and_test_sizes(self):
        data = SCRIPT.parents[1] / "data"
        self.assertEqual(len(MODULE.read_dialogues(data / "validacao.txt")), 40)
        self.assertEqual(len(MODULE.read_dialogues(data / "teste.txt")), 12)


if __name__ == "__main__":
    unittest.main()
