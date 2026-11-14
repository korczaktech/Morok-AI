# Morok AI

Projeto de pesquisa e desenvolvimento de uma inteligência artificial própria, com modelo de linguagem treinado do zero, arquitetura experimental e implementação principal em C++.

 > **Estado:** infraestrutura C++20, tipos compartilhados, primitivas matemáticas, forward pass e gradientes BPTT da G1 implementados. Etapa 8: pipeline de treinamento por épocas, corpus em fluxo, divisão por documentos, validação e avaliação BPB implementados. Etapa 9: checkpoints MOROKCKP v1 integrados ao treinador, salvamento automático por época, retomada determinística, logs JSONL persistentes e exportação do melhor modelo para inferência implementados. Etapa 10: benchmark, baseline softmax-256 unigram, matriz de configurações, métricas e CI implementados; smoke test sintético executado. Validação com corpus real/licenciado ainda pendente. Etapa 11: inferência autoregressiva G1, decodificação greedy com UTF-8 estrito, CLI `morok_generate` e testes adicionados. Etapa 12: estatísticas de parâmetros, estabilidade dos gradientes, perplexidade por byte e relatórios de benchmark ampliados implementados; validação científica com corpus real ainda pendente. Etapa 13: contratos IA-CORE de configuração, ciclo de vida, contexto de requisição, cancelamento cooperativo, limites de recursos, saúde e rollback de inicialização implementados; integração real dos sete subsistemas ainda pendente. Etapa 14: IA-MEM com SQLite, memória de trabalho/episódica/semântica, busca lexical, proveniência, isolamento por sessão, retenção, exclusão auditável, backup e integridade implementados; registro final no composition root ainda pendente. Etapa 15: IA-LANG com adaptador UTF-8, contexto isolado por sessão, integração ao SessionRegistry e geração G1 via lifecycle IA-CORE implementados; persistência do contexto e streaming ainda pendentes. Etapa 16: IA-COMM com conversas, mensagens tipadas, histórico limitado, correlação por RequestId, papéis system/user/assistant/tool, erros tipados e eventos de streaming em fronteiras UTF-8 implementados; histórico volátil e streaming após geração completa. Etapa 17: IA-REAS com planejador simbólico determinístico, tarefas e operações tipadas, ordenação topológica, detecção de ciclos, limites de profundidade/orçamento, pré/pós-condições, fallback controlado, políticas de falha, cancelamento e isolamento por RequestId implementados; não interpreta linguagem natural nem representa raciocínio geral. Etapa 18: IA-LEARN com execução serializada de experimentos G1, política habilitável, limites, cópia isolada de parâmetros, relatórios, avaliação e lifecycle IA-CORE implementados; cancelamento cooperativo entre leituras/blocos está integrado; promoção automática continua fora do escopo.

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
35. [Etapa 10 — Benchmark e validação experimental](docs/35-etapa-10-benchmark-validacao.md)
36. [Etapa 11 — Geração autoregressiva da G1](docs/36-etapa-11-geracao-g1.md)
37. [Etapa 12 — Avaliação e benchmarks](docs/37-etapa-12-avaliacao-benchmarks.md)
38. [Etapa 13 — IA-CORE e contratos](docs/38-etapa-13-ia-core-e-contratos.md)
39. [Etapa 14 — IA-MEM](docs/39-etapa-14-ia-mem.md)
40. [Etapa 15 — IA-LANG](docs/40-etapa-15-ia-lang.md)
41. [Etapa 16 — IA-COMM](docs/41-etapa-16-ia-comm.md)
42. [Etapa 17 — IA-REAS](docs/42-etapa-17-ia-reas.md)
43. [Etapa 18 — IA-LEARN](docs/43-etapa-18-ia-learn.md)

## Estado atual

- Etapa 1: infraestrutura CMake/C++20, presets, sanitizers, teste de fumaça e CI adicionados.
- Etapa 2: tipos comuns, validação UTF-8, IDs fortes, erros tipados e testes adicionados.
- Etapa 3: primitivas matemáticas estáveis e testes numéricos adicionados; CI GCC/Clang Debug/Release aprovado no run 43514960300.
- Etapa 4: inventário tipado de 1.000 parâmetros FP32, ordem canônica e inicialização determinística; CI GCC/Clang Debug/Release aprovado.
- Etapa 5: forward pass fast/slow, cabeça binária causal, scoring em nats e testes adicionados; CI GCC/Clang Debug/Release aprovado no run 43517463554.
- Etapa 6: BPTT truncado e gradientes manuais da G1 implementados.
- Etapa 7: AdamW, SGD com momentum, clipping global e testes transacionais implementados.
- Etapa 8: leitura/validação de corpus em fluxo, deduplicação exata, split determinístico por documento, treino BPTT em blocos, avaliação BPB, early stopping e CLI implementados.
- Etapa 9: formato MOROKCKP v1, CRC32, persistência de parâmetros/otimizador/RNG/cursor, fsync de arquivo e diretório, salvamento ao final de cada época, validação de compatibilidade, retomada determinística, logs JSONL append-only e exportação do melhor modelo de inferência integrados ao treinador.
- Etapa 10: executável `morok_benchmark`, baseline categórico softmax-256 unigram, matriz de comparações controladas de BPTT, otimizador e learning rate, com repetições por sementes, métricas BPB/throughput/tempo/RSS, JSONL, testes e workflow de benchmark implementados. O fixture sintético valida o pipeline; avaliação linguística real aguarda corpus licenciado.
- Etapa 11: API de geração autoregressiva, leitura de checkpoint de inferência, prompt UTF-8 estrito, decodificação greedy limitada por bytes, preservação de UTF-8 e CLI `morok_generate` implementados; qualidade linguística não validada.
- Etapa 12: biblioteca de avaliação com estatísticas globais e por grupo dos 1.000 parâmetros, perplexidade por byte, normas médias/máximas de gradientes por época e relatório JSONL ampliado implementados; corpus real, baseline neural comparável e ablações arquiteturais continuam pendentes.
- Etapa 13: IA-CORE possui contratos C++20 para configuração validada, lifecycle, inicialização/rollback/encerramento, RequestContext, registro limitado de sessões, cancelamento atômico cooperativo, limites e health report; teste unitário registrado no CTest.
- Etapa 14: IA-MEM usa SQLite/WAL, FTS5 quando disponível, migração versionada, memória de trabalho/episódica/semântica, promoção explícita, proveniência, isolamento por SessionId, busca limitada por tempo/contexto, retenção, auditoria de exclusão, backup e integrity_check; teste `memory.store` registrado no CTest. O adaptador `MemorySubsystem` integra configuração IA-CORE e limpeza de memória de trabalho ao fechar sessão; o composition root ainda deve registrar os callbacks.
- Etapa 15: IA-LANG fornece `LanguageSubsystem`, valida os parâmetros G1, integra callbacks ao Runtime, cria/fecha sessões no `SessionRegistry`, mantém contexto UTF-8 isolado, aplica limites de entrada/contexto/geração, suporta reset e chama o decoder autoregressivo G1. O teste `lang.language_subsystem` está registrado no CTest. As asserções dos testes permanecem ativas também em builds Release. Identificação de idioma, tokenizer, streaming e persistência do contexto não fazem parte desta versão.
- Etapa 16: IA-COMM fornece criação/fechamento de conversas integradas às sessões IA-LANG/IA-CORE, RequestContext por conversa, mensagens system/user/assistant/tool, MessageId/ConversationId tipados, histórico ordenado com limites, correlação por RequestId, erros formatados e eventos de streaming com fragmentos UTF-8. O teste `comm.communication_subsystem` está registrado no CTest. O streaming é fragmentação após geração completa; histórico é volátil.
- Etapa 17: IA-REAS fornece tarefas estruturadas, contexto autorizado explícito, operações determinísticas matemáticas/lógicas, dependências declaradas e inferidas, ordenação topológica estável, detecção de ciclos, orçamento de 32 etapas, profundidade máxima 8, pré/pós-condições, fallback único, políticas abort_plan/continue_independent, cancelamento e vínculo de plano a SessionId/RequestId. O teste `reason.reasoning_subsystem` está registrado no CTest. Não interpreta prompts livres, não executa ferramentas e não afirma raciocínio geral.
- Etapa 18: IA-LEARN fornece `LearningSubsystem`, lifecycle IA-CORE, política explícita para habilitar treinamento, limites de experimentos, execução serializada, treinamento sobre cópia privada dos parâmetros, validação do candidato, estatísticas e relatório com RequestId. O teste `learn.learning_subsystem` está registrado no CTest. Não há treinamento online nem promoção automática; cancelamento cooperativo é consultado durante varredura, documentos, blocos BPTT e avaliação.
- Protótipo experimental `morok_complete`: lê `entrada.txt`, treina a G1 do zero sobre a sequência, gera uma continuação ASCII e grava `saida.txt`. O GitHub Actions executa esse fluxo quando `entrada.txt` muda na `main`; os parâmetros não persistem entre execuções.
- G1: inventário de parâmetros, forward pass, gradientes B0 e otimizadores implementados; os 29 parâmetros do segmentador permanecem fora do caminho preditivo.

## Próximas etapas

1. Executar campanha de benchmark com corpus real/licenciado e múltiplas sementes em hardware de referência.
2. Analisar estabilidade, qualidade linguística, BPB, latência, throughput e limite de RAM de 4 GB.
3. Implementar segmentação reversível e integrar os subsistemas gradualmente.

## Princípio de engenharia

Toda hipótese deve ser testável. Uma otimização só será considerada válida quando comparada com uma referência simples, usando dados, hardware e condições equivalentes.
