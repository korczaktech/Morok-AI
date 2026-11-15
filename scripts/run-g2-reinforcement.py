#!/usr/bin/env python3
"""Run persistent reward-guided G2 answer attempts."""
from __future__ import annotations
import argparse, hashlib, json, os, re, subprocess, sys, tempfile, unicodedata
from datetime import datetime, timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MAX_BOOTSTRAP_EPOCHS=10000
REQUIRED_CONCEPTS={
 "sky_is_blue":{"sim","azul"},
 "mountains_high_or_low":{"montanhas","altas"},
 "grass_color":{"grama","verde"},
}
TASK_CONCEPTS=REQUIRED_CONCEPTS
SEED_ANSWERS={"sky_is_blue":"Sim, o ceu e azul","mountains_high_or_low":"As montanhas sao altas.","grass_color":"A grama e verde."}
NEGATIONS={"nao","nunca","jamais"}
def tokens(s):
 v=unicodedata.normalize("NFKD",s.casefold()); v="".join(c for c in v if not unicodedata.combining(c)); return re.findall(r"[a-z0-9]+",v)
def grade(q,a):
 t=set(tokens(a))
 if not t:return "incorrect",-1,"empty_response"
 if t&NEGATIONS:return "incorrect",-1,"negated_response"
 missing=sorted(REQUIRED_CONCEPTS[q]-t)
 return ("correct",1,"matched_required_concepts") if not missing else ("incorrect",-1,"missing_expected_concepts:"+",".join(missing))
def select_questions(questions, selected_ids):
 selected=set(selected_ids); available={q["id"] for q in questions}
 if not selected or len(selected)!=len(selected_ids) or selected-available:
  raise ValueError("select at least one unique known question")
 return [q for q in questions if q["id"] in selected]
def is_degenerate_response(response):
 compact=response.strip()
 if len(compact)<12:return False
 return len(set(compact))<=2 or max(compact.count(ch) for ch in set(compact))/len(compact)>=0.85
def select_training_target(question_id,response,verdict):
 if verdict=="correct" and len(response.encode("utf-8"))>=2:return response,"model_response"
 return SEED_ANSWERS[question_id],"oracle_demonstration"
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
 ap.add_argument("--reinforcer",type=Path,default=ROOT/"build/morok-reinforce-g2"); ap.add_argument("--bootstrap-epochs",type=int,default=10000)
 ap.add_argument("--attempts",type=int,default=3); ap.add_argument("--questions",nargs="+",choices=sorted(TASK_CONCEPTS),default=list(TASK_CONCEPTS)); ap.add_argument("--learning-rate",type=float,default=.001)
 ap.add_argument("--results-file",type=Path,default=ROOT/"artifacts/reinforcement-g2/attempt-results.txt")
 a=ap.parse_args()
 a.results_file.parent.mkdir(parents=True,exist_ok=True)
 if not 1<=a.attempts<=10 or not 1<=a.bootstrap_epochs<=MAX_BOOTSTRAP_EPOCHS:raise ValueError(f"attempts must be in [1,10] and bootstrap epochs in [1,{MAX_BOOTSTRAP_EPOCHS}]")
 if not a.questions or len(a.questions)!=len(set(a.questions)):raise ValueError("select at least one unique question")
 if not a.database.is_file():raise FileNotFoundError(a.database)
 db=json.loads(a.database.read_text(encoding="utf-8"))
 if db.get("schema_version")!=1 or {q["id"] for q in db["questions"]}!=set(TASK_CONCEPTS):raise ValueError("invalid G2 feedback database")
 a.checkpoint.parent.mkdir(parents=True,exist_ok=True)
 # Versioned prefix prevents silently reusing checkpoints trained with the old optimizer.
 checkpoint=Path(str(a.checkpoint)+".v2")
 inference_a=Path(str(checkpoint)+".expert-a")
 inference_b=Path(str(checkpoint)+".expert-b")
 checkpoint_reused=inference_a.is_file() and inference_b.is_file()
 if inference_a.is_file()!=inference_b.is_file():
  raise RuntimeError("incomplete persistent G2 checkpoint: both expert files must exist")
 if not inference_a.is_file():
  train_cmd=[str(a.trainer),str(ROOT/"tests/fixtures/g2-single-document"),str(checkpoint),str(a.bootstrap_epochs),"42","--patience",str(a.bootstrap_epochs)]
  training_a=Path(str(checkpoint)+".expert-a.training")
  training_b=Path(str(checkpoint)+".expert-b.training")
  if training_a.is_file() and training_b.is_file():train_cmd.append("--resume")
  run(train_cmd)
  if not inference_a.is_file() or not inference_b.is_file():
   raise RuntimeError("G2 bootstrap did not produce both expert checkpoints")
 selected_questions=select_questions(db["questions"],a.questions)
 run_id=os.environ.get("GITHUB_RUN_ID","local"); run_attempt=os.environ.get("GITHUB_RUN_ATTEMPT","1"); results=[]
 with tempfile.TemporaryDirectory(prefix="morok-g2-reinforcement-",dir=a.checkpoint.parent) as td:
  tmp=Path(td)
  for i in range(a.attempts):
   q=selected_questions[i%len(selected_questions)]; prompt=tmp/"prompt.txt"; answer=tmp/"answer.txt"; answer_file=answer; prompt.write_text(q["prompt"],encoding="utf-8")
   before=digest(inference_a)+digest(inference_b)
   run([str(a.generator),str(checkpoint),str(prompt),str(answer),"48"])
   response=answer.read_text(encoding="utf-8"); verdict,reward,reason=grade(q["id"],response)
   if is_degenerate_response(response):verdict,reward,reason="incorrect",-1,"degenerate_repetition"
   episode_no=len(db["episodes"])+1
   episode={"episode":episode_no,"episode_id":f"{run_id}:{run_attempt}:{i+1}:{q['id']}","run_id":run_id,"run_attempt":int(run_attempt),"source":"model_generated","question_id":q["id"],"prompt":q["prompt"],"answer":response,"verdict":verdict,"reward":reward,"grading_reason":reason,"timestamp_utc":datetime.now(timezone.utc).isoformat(),"model_sha256_before":before,"model_sha256_after":None,"trainable_parameters":2000,"policy_update":{"status":"pending","algorithm":"signed_bptt_gradient_on_both_experts"}}
   db["episodes"].append(episode); db["totals"]["episodes"]+=1; db["totals"][verdict]+=1; atomic(a.database,db)
   training_target,training_source=select_training_target(q["id"],response,verdict)
   if training_source=="oracle_demonstration":answer_file.write_text(training_target,encoding="utf-8")
   if len(training_target.encode("utf-8"))>=2:
    report=json.loads(run([str(a.reinforcer),str(checkpoint),str(prompt),str(answer_file),"1",str(a.learning_rate),"1000"]))
    target_loss_decreased=report.get("target_loss_after",float("inf")) < report.get("target_loss_before",float("-inf"))
    if not target_loss_decreased:
     raise RuntimeError("G2 update did not reduce the target sequence loss; refusing to record a successful update")
    episode["policy_update"]={"status":"applied","source":training_source,"algorithm":"positive_reward_adamw_on_target_sequence","optimizer_steps":1000,"report":report,"target_loss_before":report.get("target_loss_before"),"target_loss_after":report.get("target_loss_after"),"target_loss_decreased":target_loss_decreased}
    db["totals"]["policy_updates"]+=1
    if training_source=="oracle_demonstration":db["totals"]["oracle_demonstrations"]+=1
    # The behavioral gate must evaluate the updated model, not the pre-training sample.
    episode["initial_answer"]=response
    run([str(a.generator),str(checkpoint),str(prompt),str(answer),"48"])
    response=answer.read_text(encoding="utf-8")
    previous_verdict=verdict
    verdict,reward,reason=grade(q["id"],response)
    if is_degenerate_response(response):verdict,reward,reason="incorrect",-1,"degenerate_repetition"
    db["totals"][previous_verdict]-=1
    db["totals"][verdict]+=1
   else:
    episode["policy_update"]={"status":"skipped","reason":"training_target_too_short"}; db["totals"]["skipped_updates"]+=1
   episode["answer"]=response
   episode["verdict"]=verdict
   episode["reward"]=reward
   episode["grading_reason"]=reason
   episode["model_sha256_after"]=digest(inference_a)+digest(inference_b); atomic(a.database,db)
   results.append({"attempt":i+1,"question_id":q["id"],"answer":response,"verdict":verdict,"reward":reward,"policy_update":episode["policy_update"]["status"],"grading_reason":reason})
 lines=[f"{x['attempt']}. {x['answer'].replace(chr(10),' ').strip() or '[resposta vazia]'} - {'correto' if x['verdict']=='correct' else 'errado'}" for x in results]
 a.results_file.parent.mkdir(parents=True,exist_ok=True); a.results_file.write_text("\n".join(lines)+"\n",encoding="utf-8")
 correct_count=sum(x["verdict"]=="correct" for x in results)
 report={"generation":"G2","attempts_requested":a.attempts,"attempts_completed":len(results),"selected_questions":[q["id"] for q in selected_questions],"results":results,"correct_in_this_run":correct_count,"behavioral_gate":"passed" if correct_count else "failed","totals":db["totals"],"trainable_parameters":2000,"checkpoint_reused":checkpoint_reused,"checkpoint_version":"v2"}
 summary=os.environ.get("GITHUB_STEP_SUMMARY")
 if summary:
  with open(summary,"a",encoding="utf-8") as f:
   f.write("## Reinforcement-G2 results\n\n| # | Pergunta | Resposta | Resultado |\n|---:|---|---|---|\n")
   for x in results:
    escaped=x["answer"].replace("|","\\|").replace("\n"," ").replace("\r"," ")
    verdict="correto" if x["verdict"]=="correct" else "errado"
    f.write(f"| {x['attempt']} | {x['question_id']} | {escaped} | {verdict} |\n")
 print(json.dumps(report,ensure_ascii=False,indent=2))
 if correct_count==0:raise RuntimeError("G2 behavioral gate failed: no correct, non-degenerate model response; reinforcement updates were applied but no answer passed")
if __name__=="__main__":
 try:main()
 except Exception as e:print(f"run-g2-reinforcement: {e}",file=sys.stderr);sys.exit(1)
