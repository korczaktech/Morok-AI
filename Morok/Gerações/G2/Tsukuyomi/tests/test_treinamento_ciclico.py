import sys
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1] / "scripts"
DATA_DIR = Path(__file__).resolve().parents[1] / "data"
sys.path.insert(0, str(SCRIPT_DIR))

import treinamento_ciclico as training


class TrainingDataTests(unittest.TestCase):
    def test_split_sizes(self):
        self.assertEqual(len(training.read_dialogues(DATA_DIR / "treinamento.txt")), 32)
        self.assertEqual(len(training.read_dialogues(DATA_DIR / "validacao.txt")), 4)
        self.assertEqual(len(training.read_dialogues(DATA_DIR / "teste.txt")), 4)

    def test_splits_have_no_question_overlap(self):
        groups = [
            {question for question, _ in training.read_dialogues(DATA_DIR / name)}
            for name in ("treinamento.txt", "validacao.txt", "teste.txt")
        ]
        self.assertTrue(groups[0].isdisjoint(groups[1]))
        self.assertTrue(groups[0].isdisjoint(groups[2]))
        self.assertTrue(groups[1].isdisjoint(groups[2]))

    def test_exact_answer_scores_one(self):
        correct, score = training.evaluate("Sim, o céu é azul.", "Sim, o céu é azul.")
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
