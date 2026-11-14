import json
import runpy
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
MODULE=runpy.run_path(str(ROOT/"scripts"/"run-g2-reinforcement.py"))
grade=MODULE["grade"]
class G2ReinforcementTests(unittest.TestCase):
 def test_grades_three_controlled_questions(self):
  self.assertEqual(grade("sky_is_blue","Sim, o céu é azul!")[0:2],("correct",1))
  self.assertEqual(grade("mountains_high_or_low","As montanhas são altas.")[0:2],("correct",1))
  self.assertEqual(grade("grass_color","A grama é verde.")[0:2],("correct",1))
 def test_rejects_negation_and_wrong_concept(self):
  self.assertEqual(grade("sky_is_blue","Não, o céu não é azul.")[0:2],("incorrect",-1))
  self.assertEqual(grade("mountains_high_or_low","As montanhas são baixas.")[0:2],("incorrect",-1))
 def test_database_is_isolated_and_has_2000_parameter_policy(self):
  db=json.loads((ROOT/"data"/"reinforcement-g2"/"answers.json").read_text(encoding="utf-8"))
  self.assertEqual(db["policy"]["trainable_parameters"],2000)
  self.assertEqual(db["totals"]["episodes"],len(db["episodes"]))
  self.assertEqual(len(db["episodes"]),0)
if __name__=="__main__":unittest.main()
