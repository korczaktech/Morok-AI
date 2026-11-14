#!/usr/bin/env python3
"""Run isolated reward-driven G2 answer attempts."""
from __future__ import annotations
import argparse, hashlib, json, os, re, subprocess, sys, tempfile, unicodedata
from datetime import datetime, timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
TASK_CONCEPTS={"sky_is_blue":{"sim","azul"},"mountains_high_or_low":{"altas"},"grass_color":{"verde"}}
SEED_ANSWERS={"sky_is_blue":"Sim.","mountains_high_or_low":"Altas.","grass_color":"Verde."}
NEGATIONS={"nao","nunca","jamais"}
def tokens(s):
 v=unicodedata.normalize("NFKD",s.casefold()); v="".join(c for c in v if not unicodedata.combining(c)); return re.findall(r"[a-z0-9]+",v)
def grade(q,a):
 t=set(tokens(a))
 if not t:return "incorrect",-1,"empty_response"
 if t&NEGATIONS:return "incorrect",-1,"negated_response"
 m=sorted(t&TASK_CONCEPTS[q])
 return ("correct",1,"matched_expected_concept:"+",".join(m)) if m else ("incorrect",-1,"expected_concept_not_found")
def run(cmd):
 r=subprocess.run(cmd,capture_output=True,text=True,encoding="utf-8")
 if r.returncode:raise RuntimeError(f"command failed: {' '.join(cmd)}\n{r.stdout}\n{r.stderr}")
 return r.stdout.strip()
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def atomic(p,d):
 p.parent.mkdir(parents=True,exist_ok=True); t=p.with_name(p.name+".tmp"); t.write_text(json.dumps(d,ensure_ascii=False,indent=2)+"\n",encoding="utf-8"); os.replace(t,p)
def main():
 ap=argparse.ArgumentParser()
 ap.add_argument("--checkpoint",type=Path,required=True); ap.add_argument("--database",type=Path,default=ROOT/"data/reinforcement-g2/answers.json")
 ap.add_argument("--trainer",type=Path,default=ROOT/"build/morok-train-g2"); ap.add_argument("--generator",type=Path,default=ROOT/"build/morok-generate-g2")
 ap.add_argument("--reinforcer",type=Path,default=ROOT/"build/morok-reinforce-g2"); ap.add_argument("--bootstrap-epochs",type=int,default=8)
 ap.add_argument("--attempts",type=int,default=3); ap.add_argument("--learning-rate",type=float,default=.0001)
 ap.add_argument("--results-file",type=Path,default=ROOT/"artifacts/reinforcement-g2/attempt-results.txt")
 a=ap.parse_args()
 if not 1<=a.attempts<=100 or not 1<=a.bootstrap_epochs<=100:raise ValueError("attempts and bootstrap epochs must be in [1,100]")
 if not a.database.is_file():raise FileNotFoundError(a.database)
 db=json.loads(a.database.read_text(encoding="utf-8"))
 if db.get("schema_version")!=1 or {q["id"] for q in db["questions"]}!=set(TASK_CONCEPTS):raise ValueError("invalid G2 feedback database")
 a.checkpoint.parent.mkdir(parents=True,exist_ok=True)
 if not Path(str(a.checkpoint)+".expert-a").is_file() or not Path(str(a.checkpoint)+".expert-b").is_file():
  run([str(a.trainer),str(ROOT/"tests/fixtures/benchmark"),str(a.checkpoint),str(a.bootstrap_epochs),"42"])
 run_id=os.environ.get("GITHUB_RUN_ID","local"); run_attempt=os.environ.get("GITHUB_RUN_ATTEMPT","1"); results=[]
 with tempfile.TemporaryDirectory(prefix="morok-g2-reinforcement-") as td:
  tmp=Path(td)
  for i in range(a.attempts):
   q=db["questions"][i%len(db["questions"])]; prompt=tmp/"prompt.txt"; answer=tmp/"answer.txt"; prompt.write_text(q["prompt"],encoding="utf-8")
   before=digest(Path(str(a.checkpoint)+".expert-a"))+digest(Path(str(a.checkpoint)+".expert-b"))
   run([str(a.generator),str(a.checkpoint),str(prompt),str(answer),"48"])
   response=answer.read_text(encoding="utf-8"); verdict,reward,reason=grade(q["id"],response); episode_no=len(db["episodes"])+1
   episode={"episode":episode_no,"episode_id":f"{run_id}:{run_attempt}:{i+1}:{q['id']}","run_id":run_id,"run_attempt":int(run_attempt),"source":"model_generated","question_id":q["id"],"prompt":q["prompt"],"answer":response,"verdict":verdict,"reward":reward,"grading_reason":reason,"timestamp_utc":datetime.now(timezone.utc).isoformat(),"model_sha256_before":before,"model_sha256_after":None,"trainable_parameters":2000,"policy_update":{"status":"pending","algorithm":"reward_weighted_bptt_score_gradient_on_both_experts"}}
   db["episodes"].append(episode); db["totals"]["episodes"]+=1; db["totals"][verdict]+=1; atomic(a.database,db)
   if len(response.encode("utf-8"))>=2:
    prompt_file=tmp/"prompt.txt"; answer_file=tmp/"answer.txt"
    report=json.loads(run([str(a.reinforcer),str(a.checkpoint),str(prompt_file),str(answer_file),str(reward),str(a.learning_rate)]))
    episode["policy_update"]={"status":"applied","algorithm":"reward_weighted_bptt_score_gradient_on_both_experts","report":report}; db["totals"]["policy_updates"]+=1
   else:episode["policy_update"]={"status":"skipped","reason":"response_too_short_for_bptt"}; db["totals"]["skipped_updates"]+=1
   episode["model_sha256_after"]=digest(Path(str(a.checkpoint)+".expert-a"))+digest(Path(str(a.checkpoint)+".expert-b")); atomic(a.database,db)
   results.append({"attempt":i+1,"question_id":q["id"],"answer":response,"verdict":verdict,"reward":reward,"policy_update":episode["policy_update"]["status"]})
 lines=[f"{x['attempt']}. {x['answer'].replace(chr(10),' ').strip() or '[resposta vazia]'} - {'correto' if x['verdict']=='correct' else 'errado'}" for x in results]
 a.results_file.parent.mkdir(parents=True,exist_ok=True); a.results_file.write_text("\n".join(lines)+"\n",encoding="utf-8")
 report={"generation":"G2","attempts_requested":a.attempts,"attempts_completed":len(results),"results":results,"totals":db["totals"],"trainable_parameters":2000}
 summary=os.environ.get("GITHUB_STEP_SUMMARY")
 if summary:
  with open(summary,"a",encoding="utf-8") as f:
   f.write("## Reinforcement-G2 results\n\n| # | Pergunta | Resposta | Resultado |\n|---:|---|---|---|\n")
   for x in results:
    escaped=x["answer"].replace("|","\\\\|").replace("\n"," ").replace("\r"," ")
    verdict="correto" if x["verdict"]=="correct" else "errado"
    f.write(f"| {x['attempt']} | {x['question_id']} | {escaped} | {verdict} |\n")
 print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=="__main__":
 try:main()
 except Exception as e:print(f"run-g2-reinforcement: {e}",file=sys.stderr);sys.exit(1)
