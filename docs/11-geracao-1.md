# 11. Geração 1 — orçamento de 1.000 parâmetros

## 11.1 Objetivo

Construir uma primeira instância treinável da arquitetura, com exatamente 1.000 parâmetros treináveis e capacidade de executar experimentos de predição de bytes.

A G1 é uma prova de engenharia e pesquisa, não uma promessa de assistente conversacional competente.

## 11.2 Orçamento conceitual aprovado

| Componente | Orçamento |
|---|---:|
| Núcleo recorrente | 400 |
| Saída preditiva | 136 |
| Segmentação | 128 |
| Representação hierárquica | 256 |
| Controle contextual | 80 |
| **Total** | **1.000** |

A soma fecha em 1.000. Entretanto, a distribuição ainda não está convertida em dimensões concretas de matrizes, vetores e vieses. A implementação deverá produzir um inventário automático de parâmetros e provar que a contagem real é exatamente 1.000.

Se as dimensões necessárias não couberem nesse orçamento, a arquitetura deverá ser revisada antes de alterar a contagem aprovada.

## 11.3 Precisão inicial

FP32. Os parâmetros treináveis ocupariam teoricamente 4.000 bytes, sem contar gradientes, estados do otimizador, buffers, metadados e estruturas auxiliares. O consumo real será maior e deverá ser medido.

## 11.4 Requisitos mínimos

- inicialização aleatória reproduzível;
- treinamento sem pesos pré-treinados;
- inferência independente;
- entrada UTF-8 convertida em bytes;
- mecanismo de predição;
- representação de agrupamentos;
- reconstrução sem perdas no caminho em que for exigida;
- inventário de parâmetros;
- checkpoints;
- testes automatizados;
- medição de RAM e tempo.

## 11.5 O que não será prometido

Uma G1 de 1.000 parâmetros terá capacidade extremamente limitada. O objetivo é verificar o pipeline de aprendizado, a contagem, a estabilidade e as hipóteses de representação. Não se espera que produza conversação geral de qualidade.
