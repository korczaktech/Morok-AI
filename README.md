# Morok AI

Projeto de pesquisa e desenvolvimento de uma inteligência artificial própria, com modelo de linguagem treinado do zero, arquitetura experimental e implementação principal em C++.

 > **Estado:** infraestrutura C++20, tipos compartilhados, primitivas matemáticas, forward pass e gradientes BPTT da G1 implementados. Etapa 8: pipeline de treinamento por épocas, corpus em fluxo, divisão por documentos, validação e avaliação BPB implementados. Etapa 9: checkpoints MOROKCKP v1 integrados ao treinador, salvamento automático por época, retomada determinística e logs JSONL persistentes implementados; testes de equivalência após retomada adicionados.

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
25. [Etapa 1 — Infraestrutura inicial](docs/25-etapa-1-infraestrutura.md)
26. [Etapa 2 — Tipos compartilhados](docs/26-etapa-2-tipos-compartilhados.md)
27. [Etapa 3 — Primitivas matemáticas](docs/27-etapa-3-primitivas-matematicas.md)
28. [Etapa 4 — Inventário e inicialização dos parâmetros G1](docs/28-etapa-4-inventario-parametros-g1.md)
29. [Etapa 5 — Forward pass da G1](docs/29-etapa-5-forward-pass-g1.md)
30. [Etapa 6 — BPTT e gradientes da G1](docs/30-etapa-6-bptt-gradientes-g1.md)
31. [Etapa 7 — Otimizadores e clipping global da G1](docs/31-etapa-7-otimizadores-g1.md)
32. [Etapa 8 — Pipeline de treinamento da G1](docs/32-etapa-8-treinamento-g1.md)
33. [GitHub Actions: entrada.txt -> saida.txt](docs/33-github-actions-entrada-saida.md)
34. [Etapa 9 — Checkpoints e persistência](docs/34-etapa-9-checkpoints-persistencia.md)

## Estado atual

- Etapa 1: infraestrutura CMake/C++20, presets, sanitizers, teste de fumaça e CI adicionados.
- Etapa 2: tipos comuns, validação UTF-8, IDs fortes, erros tipados e testes adicionados.
- Etapa 3: primitivas matemáticas estáveis e testes numéricos adicionados; CI GCC/Clang Debug/Release aprovado no run 43514960300.
- Etapa 4: inventário tipado de 1.000 parâmetros FP32, ordem canônica e inicialização determinística; CI GCC/Clang Debug/Release aprovado.
- Etapa 5: forward pass fast/slow, cabeça binária causal, scoring em nats e testes adicionados; CI GCC/Clang Debug/Release aprovado no run 43517463554.
- Etapa 6: BPTT truncado e gradientes manuais da G1 implementados.
- Etapa 7: AdamW, SGD com momentum, clipping global e testes transacionais implementados.
- Etapa 8: leitura/validação de corpus em fluxo, deduplicação exata, split determinístico por documento, treino BPTT em blocos, avaliação BPB, early stopping e CLI implementados.
- Etapa 9: formato MOROKCKP v1, CRC32, persistência de parâmetros/otimizador/RNG/cursor, fsync de arquivo e diretório, salvamento ao final de cada época, validação de compatibilidade, retomada determinística e logs JSONL append-only integrados ao treinador.
- Protótipo experimental `morok_complete`: lê `entrada.txt`, treina a G1 do zero sobre a sequência, gera uma continuação ASCII e grava `saida.txt`. O GitHub Actions executa esse fluxo quando `entrada.txt` muda na `main`; os parâmetros não persistem entre execuções.
- G1: inventário de parâmetros, forward pass, gradientes B0 e otimizadores implementados; os 29 parâmetros do segmentador permanecem fora do caminho preditivo.

## Próximas etapas

1. Executar o primeiro treinamento com corpus licenciado e registrar RSS, throughput e BPB.
2. Implementar relatório de benchmark com RSS, throughput e comparação de BPB.
3. Implementar segmentação reversível e integrar os subsistemas gradualmente.

## Princípio de engenharia

Toda hipótese deve ser testável. Uma otimização só será considerada válida quando comparada com uma referência simples, usando dados, hardware e condições equivalentes.
