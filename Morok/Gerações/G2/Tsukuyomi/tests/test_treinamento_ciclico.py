import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1] / "scripts"
DATA_DIR = Path(__file__).resolve().parents[1] / "data"
sys.path.insert(0, str(SCRIPT_DIR))

import treinamento_ciclico as training


class TrainingDataTests(unittest.TestCase):
    def test_split_sizes(self):
        self.assertEqual(len(training.read_dialogues(DATA_DIR / "treinamento.txt")), 32)
        self.assertEqual(len(training.read_dialogues(DATA_DIR / "validacao.txt")), 40)
        self.assertEqual(len(training.read_dialogues(DATA_DIR / "teste.txt")), 4)

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
