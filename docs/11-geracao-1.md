# 11. Geração 1 — orçamento de 1.000 parâmetros

## 11.1 Objetivo

Construir uma primeira instância treinável da RLAH com exatamente 1.000 parâmetros treináveis, entrada de bytes e previsão autoregressiva do próximo byte.

A G1 é uma prova de engenharia e pesquisa, não uma promessa de assistente conversacional competente.

## 11.2 Orçamento matemático fechado

A arquitetura proposta em documentos 13–15 usa dimensão de estado d=11 e possui o seguinte inventário:

| Componente | Forma | Parâmetros |
|---|---|---:|
| Gate rápido | 2 matrizes de entrada + 2 recorrentes, todas 11×11, mais 2 vieses de 11 | 506 |
| Estado lento | 2 matrizes 11×11 e 1 viés de 11 | 253 |
| Cabeça de predição | Matriz 8×22, viés de 8 e matriz triangular de 28 coeficientes | 212 |
| Pontuação de segmentação | 22 coeficientes e 7 vieses por comprimento | 29 |
| **Total** | | **1.000** |

Cálculo:
- Fast: 4×121 + 2×11 = 506.
- Slow: 2×121 + 11 = 253.
- Predição: 8×22 + 8 + 28 = 212.
- Segmentação: 22 + 7 = 29.
- Total: 506 + 253 + 212 + 29 = 1.000.

A representação hierárquica usa referências e estruturas reversíveis não treináveis. O controlador adaptativo de pesos é algorítmico e não adiciona parâmetros. A entrada binária direta também não usa tabela de embedding treinável.

## 11.3 Precisão e memória

FP32 será utilizado inicialmente. Os 1.000 parâmetros ocupam teoricamente 4.000 bytes. Isso não inclui gradientes, estados do otimizador, buffers, metadados ou estruturas auxiliares. O pico real de RAM deverá ser medido.

## 11.4 Regras de contagem

A implementação deverá expor inventário automático de cada matriz, vetor e viés. Teste obrigatório: a soma de parâmetros treináveis é exatamente 1.000. Estados recorrentes, contagens de frequência, cache de agrupamentos, tabelas de símbolos e buffers não são parâmetros treináveis.

Qualquer novo parâmetro treinável exige retirar a mesma quantidade de outro componente ou revisar formalmente o orçamento.

## 11.5 Requisitos mínimos

- inicialização aleatória reproduzível;
- treinamento sem pesos pré-treinados;
- inferência independente;
- entrada UTF-8 convertida em bytes;
- codificação binária direta;
- estado recorrente fast/slow;
- predição condicional dos oito bits do próximo byte;
- agrupamentos hierárquicos reversíveis;
- pontuação limitada de candidatos;
- checkpoints;
- testes automatizados;
- medição de RAM e tempo.

## 11.6 Limites

Uma G1 de 1.000 parâmetros terá capacidade extremamente limitada. O objetivo é verificar o pipeline de aprendizado, a contagem, a estabilidade e as hipóteses de representação. Não se espera conversação geral de qualidade.
