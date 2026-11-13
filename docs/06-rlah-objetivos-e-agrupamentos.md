# 6. RLAH — Objetivos matemáticos e seleção de agrupamentos

## 6.1 DEC-001 — Objetivo conjunto

**Decisão aprovada:** combinar utilidade preditiva, compressão e reconstrução.

Formulação inicial:

L_total = L_predição + λ L_compressão + μ L_reconstrução

Os coeficientes λ e μ serão definidos experimentalmente. Não há valores definitivos aprovados. Os termos deverão ser normalizados ou escalados de modo que suas magnitudes não causem dominância acidental.

## 6.2 DEC-003 — Critérios de utilidade

A seleção de agrupamentos combinará quatro critérios:
1. frequência;
2. ganho preditivo;
3. eficiência de compressão;
4. preservação de informação.

A frequência é um sinal auxiliar, não condição suficiente para criar uma unidade.

## 6.3 DEC-004 — Pesos adaptativos

**Decisão aprovada:** os pesos poderão mudar ao longo do treinamento conforme fase, qualidade observada e orçamento computacional.

Para evitar custo desnecessário, o controlador será inicialmente matemático e externo ao modelo neural. Não será criada uma rede adicional para controlar pesos.

Pesos iniciais provisórios:

| Critério | Peso inicial |
|---|---:|
| Frequência | 0,10 |
| Ganho preditivo | 0,40 |
| Compressão | 0,25 |
| Preservação | 0,25 |

A soma é 1,0. Esses valores são pontos de partida experimentais, não constantes definitivas.

O ajuste deverá ser periódico, limitado e estável. A regra exata de atualização, limites mínimo/máximo, taxa e gatilhos ainda precisa ser especificada.

## 6.4 DEC-005 — Pontuação de utilidade

Forma conceitual:

U(g) = w_f F(g) + w_p P(g) + w_c C(g) + w_r R(g)

Onde:
- g é um candidato a agrupamento;
- F é frequência normalizada;
- P é ganho preditivo normalizado;
- C é benefício de compressão normalizado;
- R é preservação de informação normalizada;
- w_f, w_p, w_c e w_r são pesos adaptativos.

Cada critério deverá ter definição computável. Em especial, o ganho preditivo deve ser medido como diferença de perda ou outra métrica definida entre modelo com e sem o agrupamento, com custo de avaliação controlado.

A pontuação é uma proposta de ordenação, não substitui restrições obrigatórias de reconstrução ou segurança.

## 6.5 Normalização e elegibilidade

Todos os critérios deverão ser colocados em escalas comparáveis, idealmente [0,1]. A normalização deverá evitar divisão por zero, valores ausentes e distorções por amostras raras.

Um candidato que viole a preservação mínima exigida deverá ser rejeitado, mesmo que sua pontuação agregada seja alta.

## 6.6 Eficiência de avaliação

Não será avaliado todo agrupamento possível. O sistema deverá gerar um conjunto limitado de candidatos com base em ocorrências locais, padrões observados e limites de tamanho.

O orçamento máximo de candidatos por posição ainda será definido e medido.

## 6.7 DEC-007 — Política de ciclo de vida

Estados conceituais:
- candidato;
- em avaliação;
- aceito;
- estável;
- em revisão;
- descartado.

A criação e o descarte utilizarão limiares diferentes (histerese) para evitar oscilações. O tamanho da janela de avaliação e os limiares numéricos serão determinados experimentalmente.

A preservação de informação funciona como restrição obrigatória. Agrupamentos que impossibilitem a reconstrução exigida serão rejeitados.

## 6.8 Pendências matemáticas

- Definir L_predição, L_compressão e L_reconstrução.
- Definir estimador de frequência.
- Definir ganho preditivo marginal.
- Definir custo real de compressão, incluindo metadados e referências.
- Definir preservação mínima e teste de reconstrução.
- Definir normalização robusta.
- Definir atualização adaptativa dos pesos.
- Definir limiares de criação, aceitação, estabilidade e descarte.
