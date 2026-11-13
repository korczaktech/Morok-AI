# Morok AI

Projeto de pesquisa e desenvolvimento de uma inteligência artificial própria, com modelo de linguagem treinado do zero, arquitetura experimental e implementação principal em C++.

> **Estado:** especificação documental da G1 e da arquitetura de sistema inicializada. Implementação, compilação, treinamento e validação experimental continuam pendentes.

## Objetivo

Construir progressivamente um sistema de IA independente de modelos externos durante a inferência. A primeira geração terá exatamente 1.000 parâmetros treináveis e será desenvolvida considerando uma máquina com 4 GB de RAM.

O projeto prioriza eficiência mensurável, modularidade, reprodutibilidade, baixo consumo de memória e validação experimental. Não serão feitas alegações de originalidade científica ou superioridade sem revisão bibliográfica e comparação experimental.

## Decisões fundamentais

- Arquitetura experimental própria.
- C++20 como linguagem principal.
- Treinamento do zero, sem pesos pré-treinados.
- Separação entre modelo, inferência, treinamento, representação linguística e sistema cognitivo.
- Sete subsistemas cognitivos: IA-CORE, IA-LANG, IA-MEM, IA-REAS, IA-LEARN, IA-COMM e IA-TOOLS.
- Progressão de parâmetros baseada em 1, 2, 5 e 10.
- G1 com 1.000 parâmetros treináveis.
- Representação linguística provisoriamente denominada **RLAH — Representação Latente Adaptativa Hierárquica**.
- Núcleo recorrente fast/slow de dois ritmos.
- Entrada binária direta dos bytes.
- Segmentação hierárquica reversível, auxiliar na G1.

## Documentação

1. [Visão geral e princípios](docs/01-visao-geral.md)
2. [Arquitetura geral](docs/02-arquitetura-geral.md)
3. [Sete subsistemas](docs/03-subsistemas.md)
4. [Escala de parâmetros](docs/04-modelo-e-parametros.md)
5. [RLAH: visão geral](docs/05-rlah-visao-geral.md)
6. [Objetivos matemáticos](docs/06-rlah-objetivos-e-agrupamentos.md)
7. [Segmentação e hierarquia](docs/07-rlah-segmentacao-e-hierarquia.md)
8. [Contexto e predição](docs/08-contexto-e-predicao.md)
9. [Treinamento e avaliação](docs/09-treinamento-e-avaliacao.md)
10. [Recursos e otimização](docs/10-recursos-e-otimizacao.md)
11. [Geração 1](docs/11-geracao-1.md)
12. [Pendências e validação](docs/12-pendencias-e-validacao.md)
13. [Arquitetura matemática RLAH](docs/13-arquitetura-matematica-rlah.md)
14. [Predição binária](docs/14-predicao-binaria-e-objetivo.md)
15. [Segmentação adaptativa](docs/15-segmentacao-adaptativa-e-criterio.md)
16. [Protocolo de treinamento](docs/16-protocolo-de-treinamento.md)
17. [Revisão bibliográfica](docs/17-revisao-bibliografica-e-posicionamento.md)
18. [Especificação operacional G1](docs/18-especificacao-operacional-g1.md)
19. [Contratos de interfaces](docs/19-contratos-de-interfaces.md)
20. [Subsistemas cognitivos](docs/20-subsistemas-cognitivos.md)
21. [Persistência e memória](docs/21-persistencia-e-memoria.md)
22. [Segurança e permissões](docs/22-seguranca-e-permissoes.md)
23. [Build, testes e distribuição](docs/23-build-testes-e-distribuicao.md)
24. [Estrutura completa do repositório — G1 a G30](docs/24-estrutura-completa-repositorio.md)

## Estado atual

A G1 está especificada documentalmente em nível suficiente para iniciar a implementação. A arquitetura de sistema possui contratos e responsabilidades iniciais para os sete subsistemas. Ainda não há implementação, compilação, treinamento ou benchmark que comprove estabilidade, velocidade ou qualidade.

## Próximas etapas

1. Implementar testes matemáticos e inventário automático.
2. Implementar o núcleo e a cabeça de predição.
3. Implementar treinamento e checkpoints.
4. Implementar segmentação reversível.
5. Implementar persistência, orquestração e contratos dos subsistemas.
6. Executar testes de aceitação, benchmarks e ablações.

## Princípio de engenharia

Toda hipótese deve ser testável. Uma otimização só será considerada válida quando comparada com uma referência simples, usando dados, hardware e condições equivalentes.
