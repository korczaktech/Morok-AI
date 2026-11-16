# Relatório — G3 Etapa 6: aprendizado por reforço

**Arquitetura:** G3 `0x0302`  
**Parâmetros:** exatamente 100.000  
**Precisão de treino:** FP32  
**Método:** REINFORCE com baseline móvel, KL contra SFT congelado e BPTT truncado

## Implementações

1. Avaliador independente: contrato separado da política, metadados de
   identidade/versão/origem, referências por prompt, auditoria com intervalos
   de pontuação esperados.
2. Recompensas e penalidades: correção lexical, aderência por bigramas,
   coerência de comprimento, formato, resposta vazia, repetição, cópia do
   prompt, excesso e déficit de comprimento.
3. REINFORCE com baseline: baseline móvel por categoria e vantagem
   recompensa-menos-baseline.
4. Penalidade KL: divergência entre política atual e snapshot SFT congelado,
   com beta padrão 0,02.
5. Candidatas: até quatro amostras por prompt, temperatura, Top-K, EOS e limite
   máximo de bytes controlados; tokens especiais indevidos são filtrados.
6. Reward hacking: sinalização de respostas positivas com comprimento muito
   abaixo da referência, cópia excessiva do prompt ou repetição.
7. Comparação: avaliação greedy no mesmo conjunto fechado de teste antes
   (SFT) e depois (RL), com recompensa, comprimento, vazios, repetição e delta.

## Integração com a GRU G3

A política concreta `G3PolicyAdapter` faz replay do contexto e do prefixo
amostrado no backend neural real. Os gradientes de logits do REINFORCE são
propagados pelas duas camadas GRU com BPTT truncado. O atualizador reutiliza
AdamW FP32, clipping global e weight decay seletivo do treinador SFT. Todos os
100.000 parâmetros podem ser atualizados; o orçamento e o identificador de
arquitetura não mudam. A política de referência é uma cópia separada e não
recebe atualizações.

## Execução

```sh
morok-g3-reinforce dataset.jsonl modelo-sft.morok modelo-rl.morok relatorio-rl.json 1
```

O treino usa apenas o split de treino por `group_id`. A comparação final usa o
split de teste. Exemplos sensíveis são excluídos do RL. O modelo de saída é
exportado como artefato FP32 validado.

## Interpretação dos resultados

O avaliador atual é lexical e usa as respostas de referência do dataset.
Portanto, a recompensa não representa um juiz semântico humano nem verifica
verdade factual. A aprovação da CI demonstra que o pipeline e os contratos
funcionam, não que o RL já melhorou a qualidade das respostas. O ganho só pode
ser confirmado executando o protocolo com um corpus real revisado e comparando
os números do relatório.
