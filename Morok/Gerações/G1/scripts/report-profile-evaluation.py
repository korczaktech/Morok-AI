#!/usr/bin/env python3
import csv, pathlib, statistics, sys, os
source, output = map(pathlib.Path, sys.argv[1:3])
with source.open(encoding="utf-8", newline="") as f:
    rows = list(csv.DictReader(f, delimiter="\t"))
labels = {"sky_is_blue":"O céu é azul?","grass_color":"Que cor é a grama?","mountains_high_or_low":"As montanhas são baixas ou altas?"}
lines = ["# Avaliação dos perfis G1", "",
 "Checkpoint: g1-best.morokckp, produzido pelo treinamento deste workflow.",
 "Mesmo checkpoint, máquina e conjunto de perguntas para os três perfis.",
 "Latência medida com relógio monotônico, somente durante a chamada de geração.",
 "O veredito é lexical e limitado ao gabarito deste experimento; não é avaliação semântica geral.", ""]
for profile in ("Tsukuyomi","Mokosh","Apollo"):
    selected=[r for r in rows if r["profile"]==profile]
    correct=sum(r["verdict"]=="correct" for r in selected)
    times=[float(r["latency_ms"]) for r in selected]
    lines += [f"## {profile}","",f"Resultado: {correct}/{len(selected)} corretas; {len(selected)-correct} incorretas.",
      f"Latência média: {statistics.mean(times):.3f} ms; total: {sum(times):.3f} ms.","",
      "| Pergunta | Resposta | Veredito | Tempo (ms) |","|---|---|---:|---:|"]
    for r in selected:
        answer=r["answer"].replace("|","\\|")
        verdict="Correto" if r["verdict"]=="correct" else "Errado"
        lines.append(f'| {labels[r["question_id"]]} | {answer} | {verdict} | {float(r["latency_ms"]):.3f} |')
    lines.append("")
averages={p:statistics.mean(float(r["latency_ms"]) for r in rows if r["profile"]==p) for p in ("Tsukuyomi","Mokosh","Apollo")}
lines += ["## Comparação de velocidade",""]
base=averages["Tsukuyomi"]
for p,avg in averages.items():
    relative=((base/avg)-1)*100 if avg else 0
    lines.append(f"- {p}: média {avg:.3f} ms; velocidade relativa a Tsukuyomi: {relative:+.2f}%.")
lines += ["","A meta de Apollo ser pelo menos 30% mais rápido exige redução de latência de pelo menos 30% em relação a Tsukuyomi. Uma execução é medição inicial, não conclusão estatística.",""]
output.write_text("\n".join(lines),encoding="utf-8")
summary=os.environ.get("GITHUB_STEP_SUMMARY")
if summary:
    with open(summary,"a",encoding="utf-8") as f: f.write("\n"+"\n".join(lines)+"\n")
print("\n".join(lines))
