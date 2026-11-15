# 6. RLAH — Objetivos matemáticos e seleção de agrupamentos

> **Atualização normativa:** este documento registra as decisões conceituais iniciais. As definições computáveis, pesos, critérios, limites e função objetivo da G1 estão nos documentos 14, 15 e 18 e prevalecem sobre formulações preliminares deste arquivo.

## 6.1 Objetivo conceitual

A proposta original combina utilidade preditiva, compressão e reconstrução. Na G1, a reconstrução é garantida estruturalmente por round-trip exato e não é uma perda neural. A função efetiva é L_total = L_pred + λ_group L_group.

## 6.2 Critérios de utilidade

A seleção de agrupamentos combina:
1. frequência;
2. associação preditiva diagnóstica;
3. eficiência de compressão líquida;
4. preservação exata.

A preservação é uma condição rígida. Frequência não é condição suficiente para criar uma unidade.

## 6.3 Pesos iniciais

| Critério | Peso inicial |
|---|---:|
| Frequência | 0,10 |
| Associação preditiva | 0,40 |
| Compressão | 0,25 |
| Preservação | 0,25 |

A política operacional do controlador e a calibração estão no documento 18.

## 6.4 Pontuação

U(g) = w_f F(g) + w_p P(g) + w_c C(g) + w_r R(g)

Todos os critérios devem ter definição computável e escala comparável. C inclui metadados e referências. R=0 rejeita o candidato antes da pontuação.

## 6.5 Eficiência de avaliação

Não será avaliado todo agrupamento possível. O sistema gera candidatos locais limitados por frequência, comprimento, orçamento de processamento e cache.

## 6.6 Ciclo de vida

Estados conceituais: candidato, em avaliação, aceito, estável, em revisão e descartado. A criação e remoção usam histerese.

## 6.7 Limite da G1

A métrica preditiva dos agrupamentos na G1 é diagnóstica e não representa redução causal da perda do preditor. O núcleo principal continua recebendo bytes individuais. Uma futura variante integrada deverá especificar como unidades agrupadas entram no modelo e deverá ser avaliada separadamente.
