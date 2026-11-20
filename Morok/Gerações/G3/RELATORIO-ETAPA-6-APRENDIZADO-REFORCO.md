# Relatório — G3 Etapa 6: aprendizado por reforço independente

**Arquitetura:** G3 `0x0302`  
**Parâmetros:** exatamente 100.000  
**Precisão de treino:** FP32  
**Método:** REINFORCE com baseline móvel e BPTT truncado  
**Inicialização:** política própria, determinada pela seed; sem pesos SFT

## Implementações

1. Avaliador separado: contrato independente da política, metadados de
   identidade/versão/origem, referências por prompt e auditoria de intervalos
   esperados.
2. Recompensa lexical: distância de edição para similaridade, F1 de bigramas
   para aderência, coerência neutra e formato baseado em resposta não vazia.
3. REINFORCE: baseline móvel por categoria e vantagem recompensa-menos-baseline.
4. Penalidades: resposta vazia, ausência de EOS, repetição, cópia do prompt e
   excesso de comprimento somente quando configurado.
5. Candidatas: até quatro amostras por prompt, temperatura, Top-K, EOS e limite
   máximo de bytes controlados.
6. Reward hacking: sinalização de respostas positivas com cópia excessiva ou
   repetição.
7. Política concreta: replay do contexto e prefixo amostrado no backend neural
   G3; gradientes propagados pelas duas camadas GRU com BPTT truncado.
8. Otimizador: AdamW FP32, clipping global e weight decay seletivo; todos os
   100.000 parâmetros podem ser atualizados.
9. CLI autônoma: recebe dataset RL, modelo de saída e relatório; inicializa a
   política do zero e avalia no split de teste. Não recebe caminho de SFT.

## Independência do SFT

O fluxo de treino RL não carrega modelo, pesos, checkpoint, estado de
otimizador, baseline ou dataset do SFT. O modelo é inicializado a partir da
seed configurada. O dataset RL contém as respostas de referência usadas pelo
avaliador lexical; isso é a fonte de recompensa do experimento e não uma
dependência do SFT.

A regularização KL contra uma política de referência continua disponível como
opção experimental na API, mas tem beta padrão zero. O CLI independente não
fornece referência e não ativa KL.

## Execução

```sh
morok-g3-reinforce rl-dataset.jsonl modelo-rl.morok relatorio-rl.json 1 42
```

O treino usa apenas o split de treino por `group_id`. A avaliação final usa o
split de teste. Exemplos sensíveis são excluídos. O artefato de saída é FP32 e
contém os metadados completos da arquitetura.

## Pendências e interpretação

O avaliador atual é lexical e usa respostas de referência do dataset RL.
Portanto, a recompensa não representa um juiz semântico humano nem verifica
verdade factual. Os casos de auditoria do CLI são sintéticos e derivados do
dataset, não um conjunto humano independente.

A CLI ainda não persiste e restaura um checkpoint RL completo entre processos
com pesos, momentos AdamW, baseline, RNG e posição exata do dataset. A CI pode
validar os contratos e o pipeline de integração, mas não demonstra ganho
empírico em um corpus de produção. Isso exige corpus revisado, execução completa
e avaliação humana independente.
