# 8. RLAH — Contexto recorrente e predição

> **Atualização normativa:** as equações, dimensões, indexação causal, saída e perda da G1 estão definidas nos documentos 13, 14, 16 e 18. Este documento é uma visão resumida e não deve ser usado isoladamente para implementar.

## 8.1 Contexto recorrente compacto

A G1 utiliza núcleo recorrente fast/slow com dimensão d=11 e estado limitado. O estado transitório não substitui a memória persistente IA-MEM.

## 8.2 Motivação de eficiência

Um estado recorrente compacto evita armazenar e comparar todas as posições anteriores a cada passo. Isso pode reduzir memória de contexto, mas pode limitar paralelismo e dependências distantes. O custo real deverá ser comparado com baselines.

## 8.3 Predição causal de bytes

Após observar e processar o byte na posição i, o modelo prevê o byte i+1. O byte-alvo e bytes futuros não podem influenciar os logits dessa previsão.

O byte é representado por oito decisões binárias condicionais. A entrada do núcleo possui 11 características fixas; os oito bits correspondem ao alvo da cabeça de saída.

## 8.4 UTF-8 e reconstrução

A entrada textual usa UTF-8 estrito. A geração acumula bytes e valida a sequência antes de apresentá-la como texto. Reconstrução sem perdas de agrupamentos é distinta da previsão probabilística.

## 8.5 Estado

Estados fast/slow iniciam em zero para uma sessão nova, são isolados por sessão e podem ser transportados entre blocos de treinamento. Na fronteira BPTT, o valor do estado é mantido, mas o grafo de gradientes é interrompido.
