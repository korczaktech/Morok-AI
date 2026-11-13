# 5. RLAH — Representação Latente Adaptativa Hierárquica

**Nome provisório:** RLAH  
**Expansão:** Representação Latente Adaptativa Hierárquica  
**Estado:** hipótese de pesquisa, não validada

## 5.1 Motivação

Investigar uma representação de linguagem que não dependa de um vocabulário fixo tradicional de palavras ou subpalavras. A proposta começa com texto UTF-8 convertido em bytes e aprende agrupamentos úteis durante o treinamento.

A ausência de um vocabulário fixo não elimina o custo de sequências longas. A RLAH deverá demonstrar que seus agrupamentos reduzem custo ou melhoram a previsão em comparação com abordagens de referência.

## 5.2 Mecanismos propostos

1. Segmentação adaptativa aprendida.
2. Compressão latente.
3. Hierarquia de unidades.
4. Contexto recorrente.
5. Predição adaptativa.

Esses mecanismos são hipóteses de projeto. A formulação matemática e as estruturas de dados ainda precisam ser fechadas.

## 5.3 Entrada

A entrada textual será validada como UTF-8 e convertida em bytes. O tratamento de entradas inválidas deverá ser especificado: rejeição, substituição controlada ou modo binário. A escolha final será documentada antes da implementação.

## 5.4 Representação hierárquica

A RLAH manterá simultaneamente:
- **Nível básico:** bytes ou unidades mínimas;
- **Nível intermediário:** agrupamentos aprendidos;
- **Nível superior:** estruturas maiores derivadas dos níveis anteriores.

Unidades maiores deverão manter referências suficientes às unidades menores para permitir reconstrução sem perdas quando essa for a tarefa.

## 5.5 Objetivo conjunto

A função inicial proposta é:

L_total = L_predição + λ L_compressão + μ L_reconstrução

Onde:
- L_predição mede o erro de previsão;
- L_compressão representa o custo da representação;
- L_reconstrução mede perda ao reconstruir a entrada;
- λ e μ controlam a contribuição dos termos.

As definições operacionais de cada termo, escalas e normalizações ainda são pendentes. A função não deverá ser implementada sem definir unidades, estimadores e comportamento numérico.

## 5.6 Limites e riscos

- Agrupamentos excessivos podem esconder estrutura útil.
- Fragmentação excessiva pode eliminar ganhos de compressão.
- Compressão com perda pode destruir informação linguística.
- Frequência não significa utilidade preditiva.
- Atualização frequente de agrupamentos pode ser cara e instável.
- Sequências de bytes podem ser maiores que sequências de subpalavras convencionais.

## 5.7 Originalidade

Byte-level modeling, segmentação aprendida e representações hierárquicas possuem precedentes na literatura. A combinação proposta não será considerada inédita sem revisão bibliográfica específica. A RLAH deverá ser comparada com trabalhos como ByT5, Charformer e CANINE, além de pesquisas contemporâneas de tokenização e compressão adaptativa.
