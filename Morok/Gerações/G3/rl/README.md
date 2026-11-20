# G3 — Aprendizado por reforço independente

O RL G3 é um processo de treinamento autônomo. Ele inicializa uma política
própria a partir de uma seed e não carrega pesos, checkpoints, datasets,
otimizadores ou estado do SFT. O modelo continua com exatamente 100.000
parâmetros, arquitetura `0x0302`, vocabulário de 261 IDs e pesos FP32 durante
o treino.

## Execução

```sh
morok-g3-reinforce Morok/Gerações/G3/rl/dataset/g3_rl_ptbr_v1.jsonl modelo-rl.morok relatorio-rl.json [epocas] [seed]
```

O executável:
1. carrega um dataset próprio do experimento RL;
2. separa os exemplos por `group_id` (85/10/5);
3. constrói um avaliador lexical versionado com as respostas de referência do
   próprio dataset RL;
4. inicializa a política G3 do zero com a seed informada;
5. atualiza os pesos somente com REINFORCE e a recompensa do avaliador;
6. avalia no split de teste, sem atualizar pesos com ele;
7. exporta o modelo FP32 e um relatório JSON.

Exemplos sensíveis e registros com `quality<1.0` são excluídos dos splits e das respostas de referência usados pelo RL. No corpus atual, isso remove registros ainda pendentes de revisão humana.
Respostas duplicadas para a mesma pergunta precisam ser idênticas; referências
conflitantes interrompem a execução em vez de escolher uma arbitrariamente.

## Algoritmo

- REINFORCE com baseline móvel por categoria.
- Quatro candidatas estocásticas por pergunta (configurável até quatro).
- Temperatura 0,7, Top-K 10 e limite de 128 bytes por candidata por padrão.
- Retorno terminal com `gamma=1.0`.
- Regularização KL contra política congelada é opcional e desativada por padrão
  (`kl_beta=0`). Quando explicitamente ativada, exige uma política de referência;
  o fluxo independente não fornece nem carrega uma política SFT.
- Gradientes de logits acumulados por batch e propagados pelas duas GRUs com
  BPTT truncado em 128 passos.
- Atualização AdamW FP32, clipping global de norma 1,0 e weight decay somente
  em matrizes.

## Avaliador e recompensa

`ReferenceRewardEvaluator` não lê logits, gradientes ou estado interno da
política. Ele recebe respostas de referência do dataset RL e calcula
similaridade por distância de edição e F1 de bigramas. O componente de coerência
fica neutro: comprimento não é usado como substituto de compreensão semântica.
A identidade, versão da rubrica e origem dos rótulos são registradas. Antes do
treino, a rubrica é testada contra casos de auditoria com limites de score.

As penalidades incluem resposta vazia, ausência de EOS, repetição e cópia de
trechos do prompt. Penalidade por resposta curta fica desativada por padrão.
O relatório marca suspeitas de reward hacking quando uma resposta obtém
recompensa positiva apesar de ser excessivamente copiada ou repetitiva.

## Relatório e limitações

O relatório registra seed, fingerprint do dataset, número de candidatas,
atualizações do otimizador, recompensa média e avaliação no split de teste.
Como o avaliador é lexical, ele não garante verdade factual, segurança, utilidade
ou qualidade semântica completa. A auditoria automatizada e os testes de CI
validam contratos e comportamento do código; não demonstram ganho empírico em
um corpus de produção.

A implementação compartilha a arquitetura neural G3 e as rotinas matemáticas
de BPTT/AdamW do motor de treinamento, mas não depende de pesos nem de uma
execução prévia de SFT.
