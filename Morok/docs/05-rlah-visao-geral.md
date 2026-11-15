# 5. RLAH — Representação Latente Adaptativa Hierárquica

**Nome provisório:** RLAH  
**Estado:** hipótese de pesquisa, não validada.

> **Atualização normativa:** este documento registra a motivação original. Para a G1, as definições matemáticas e operacionais posteriores nos documentos 13–18 prevalecem. Em particular, a G1 usa reconstrução estrutural sem perdas como restrição rígida; não treina um decoder neural de reconstrução nem aplica o termo antigo μ L_reconstrução.

## 5.1 Motivação

Investigar uma representação de linguagem que não dependa de um vocabulário fixo tradicional de palavras ou subpalavras. A proposta começa com texto UTF-8 convertido em bytes e aprende agrupamentos úteis durante o treinamento.

A ausência de vocabulário fixo não elimina o custo de sequências longas. A RLAH deverá demonstrar que seus agrupamentos reduzem custo ou melhoram a previsão em comparação com abordagens de referência.

## 5.2 Mecanismos propostos

1. Segmentação adaptativa aprendida.
2. Hierarquia de unidades.
3. Contexto recorrente.
4. Predição de bytes.
5. Avaliação de compressão reversível.

Esses mecanismos são hipóteses de projeto. A formulação normativa da G1 está nos documentos 13–18.

## 5.3 Entrada

A entrada textual da G1 é UTF-8 estrito convertido em bytes. Sequências inválidas são rejeitadas no modo textual padrão. Não há substituição silenciosa nem descarte de bytes.

## 5.4 Representação hierárquica

A RLAH mantém:
- nível básico: bytes;
- nível intermediário: agrupamentos contíguos;
- nível superior: combinações limitadas de unidades intermediárias.

Unidades maiores mantêm referências suficientes às unidades menores para reconstrução exata. Os limites e formato operacional estão no documento 18.

## 5.5 Objetivo da G1

A G1 usa L_total = L_pred + λ_group L_group. L_group treina apenas o pontuador auxiliar de candidatos; L_pred treina o núcleo e a cabeça de predição. A reconstrução exata é restrição rígida de elegibilidade, não uma perda neural.

## 5.6 Limites e riscos

- Agrupamentos excessivos podem esconder estrutura útil.
- Fragmentação excessiva pode eliminar ganhos de compressão.
- Frequência não significa utilidade preditiva.
- Atualização frequente pode ser cara e instável.
- Sequências de bytes podem ser maiores que sequências subword convencionais.
- A segmentação G1 ainda não altera o caminho preditivo byte a byte; não atribuir ganhos end-to-end sem variante integrada.

## 5.7 Originalidade

Byte-level modeling, segmentação aprendida e representações hierárquicas possuem precedentes. A combinação proposta não será considerada inédita sem revisão bibliográfica específica e experimentos controlados.
