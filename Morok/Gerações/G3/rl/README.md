# G3 — Aprendizado por reforço

A Etapa 6 usa o modelo SFT G3 como política de referência congelada e cria uma
segunda cópia treinável. A arquitetura não muda: o modelo continua com
exatamente 100.000 parâmetros, arquitetura `0x0302`, vocabulário de 261 IDs e
pesos FP32 durante o treino.

## Execução

```sh
morok-g3-reinforce dataset.jsonl modelo-sft.morok modelo-rl.morok relatorio-rl.json [epocas]
```

O executável:
1. valida e carrega o artefato FP32 SFT;
2. separa o dataset por `group_id` (85/10/5);
3. constrói o avaliador lexical independente a partir das respostas de referência;
4. usa somente o split de treino para gerar candidatas e atualizar pesos;
5. mantém o artefato SFT congelado para a penalidade KL;
6. compara SFT e RL greedy no split de teste;
7. exporta o novo artefato FP32 e um relatório JSON.

Exemplos marcados como sensíveis são excluídos do treino RL e da comparação.
O split de validação é mantido separado e não é usado para atualizar pesos.

## Algoritmo

- REINFORCE com baseline móvel por categoria.
- Quatro candidatas estocásticas por pergunta (configurável até quatro).
- Temperatura 0,7, Top-K 10 e limite de 128 bytes por candidata por padrão.
- Retorno terminal com `gamma=1.0` por padrão; descontos menores exigem justificativa experimental.
- Penalidade KL contra a política SFT congelada com `beta=0.02`.
- Gradientes de logits acumulados por batch e propagados pelas duas GRUs com
  BPTT truncado em 128 passos.
- Atualização AdamW FP32, clipping global de norma 1,0 e weight decay somente
  em matrizes. O estado de momentos é mantido durante as épocas do processo.

## Avaliador independente e recompensa

`ReferenceRewardEvaluator` não lê logits, gradientes ou estado interno da
política. Ele recebe respostas de referência e calcula similaridade por
distância de edição, F1 de bigramas e adequação de comprimento. Sua identidade,
versão de rubrica e origem dos rótulos são registradas. Antes do treino, a
rubrica é testada contra casos de auditoria com limites de score esperados.

As penalidades incluem resposta vazia, repetição, cópia de trechos do prompt,
comprimento excessivo somente quando configurado. Penalidade por resposta
curta fica desativada por padrão; o avaliador lexical não usa comprimento como
substituto de coerência, pois não consegue saber se uma resposta concisa é suficiente. O relatório também marca suspeitas de reward hacking quando uma
resposta obtém recompensa positiva apesar de ser curta demais, excessivamente
copiada ou repetitiva.

## Relatório e comparação

O relatório registra número de candidatas, atualizações do otimizador,
recompensa média, KL, suspeitas de reward hacking e, no conjunto de teste,
recompensa média, comprimento, taxa de respostas vazias e taxa de repetição
para SFT e RL, além da diferença de recompensa.

A comparação usa os mesmos prompts fechados, avaliador, limite de geração e
decodificação greedy para as duas políticas. Isso mede mudança relativa sob o
protocolo fixo; não substitui avaliação humana de qualidade semântica.

## Limitações de interpretação

A rubrica automática é lexical e baseada nas respostas do dataset. Ela não
garante verdade factual, segurança, utilidade ou qualidade semântica completa.
Os testes da CI validam algoritmo, integração, gradientes, auditoria e
serialização com fixtures sintéticas; não demonstram ganho empírico do RL em
um corpus de produção. Só um dataset real revisado e uma execução completa
podem produzir essa conclusão.
