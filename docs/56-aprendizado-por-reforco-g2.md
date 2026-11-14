# Reinforcement-G2

Este workflow aplica reforço exclusivamente à G2. A G2 possui dois especialistas independentes de 1.000 parâmetros; cada tentativa calcula gradientes BPTT e aplica a recompensa aos dois especialistas, totalizando 2.000 parâmetros treináveis.

## Isolamento

- Checkpoint G2 usa o formato pareado `.expert-a` e `.expert-b`.
- O histórico fica em `data/reinforcement-g2/answers.json`, sem compartilhar episódios com G1.
- O artefato do Actions tem nome `morok-g2-reinforcement-state`.
- O workflow é independente: `.github/workflows/reinforcement-g2.yml`.
- A geração usa `morok-generate-g2`; o reforço usa `morok-reinforce-g2`.

## Limitações

O sinal de recompensa é aplicado como gradiente de escore aproximado sobre a sequência observada; não é um estimador REINFORCE completo com baseline. O avaliador lexical é restrito às três perguntas controladas e não mede compreensão geral. Checkpoints preservam os pesos, mas o estado do otimizador é reinicializado em cada processo de reforço. Resultados de treinamento não devem ser interpretados como prova de competência linguística.
