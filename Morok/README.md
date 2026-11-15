# Morok AI

Projeto de pesquisa e desenvolvimento de uma inteligência artificial própria, com modelo de linguagem treinado do zero, arquitetura experimental e implementação principal em C++.

**Organização dos modelos:** os perfis são definidos individualmente por geração. A G1 possui somente Tsukuyomi Max, a rede neural unificada da geração. A G2 define Apollo como modelo comum, Tsukuyomi como modelo mais avançado e Mokosh como modelo rápido. A G3 permanece documental. Consulte [MODELOS.md](MODELOS.md) para a especificação, as regras de isolamento e o estado da migração.

> **Estado atual:** o Core compartilhado, o motor da G1 e  G3 permanece documental. A migração de caminhos e namespaces foi aplicada; compilação e testes locais ainda precisam ser executados para validar o commit final.

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

**Nota sobre a numeração:** a sequência operacional da G1 (por exemplo, Etapa 14 — IA-MEM em `docs/39-etapa-14-ia-mem.md`) é distinta do roteiro de 20 etapas do projeto. Neste último, a Etapa 14 é Aplicações executáveis (`docs/47-etapa-14-aplicacoes-executaveis.md`).

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
33. [Fluxo experimental de entrada e saída — automação suspensa](docs/33-github-actions-entrada-saida.md)
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
44. [Etapa 12 — IA-TOOLS](docs/45-etapa-12-ia-tools.md)
45. [Etapa 13 — Segurança](docs/46-etapa-13-seguranca.md)
46. [Etapa 14 — Aplicações executáveis (roteiro de 20 etapas)](docs/47-etapa-14-aplicacoes-executaveis.md)
47. [Etapa 15 — Testes automatizados](docs/48-etapa-15-testes-automatizados.md)
48. [Etapa 16 — Documentação e experimentos](docs/49-etapa-16-documentacao-experimentos.md)
49. [Controles complementares de segurança e integração](docs/50-etapa-18-seguranca-integracao.md)
50. [Etapa 19 — Testes de aceitação](docs/51-etapa-19-testes-aceitacao.md)
51. [Etapa 20 — Primeiro treinamento completo da G1](docs/52-etapa-20-primeiro-treinamento-g1.md)
52. [Experimento de aprendizado por reforço da G1](docs/54-aprendizado-por-reforco-g1.md)
53. [Geração 2](docs/55-geracao-2.md)

## Estado atual

- Etapa 13 — Segurança: SecuritySubsystem com negação por padrão, grants exatos por sujeito/sessão/ferramenta/capacidade, revogação, política para IA-TOOLS, confirmações humanas de uso único vinculadas à ação e aos argumentos exatos, wrapper seguro de execução IA-TOOLS, classificação imutável de origem e auditoria sem conteúdo sensível. Teste `security.security_subsystem` registrado no CTest. Sandbox de processo/OS, criptografia em repouso e detecção completa de prompt injection não estão implementadas. Consulte [Etapa 13 — Segurança](docs/46-etapa-13-seguranca.md).

- Etapa 14 — Aplicações executáveis: CLI unificada, conversor UTF-8, treinamento, avaliação BPB, benchmark, geração offline e servidor HTTP local estão implementados. A validação histórica de CI e sanitizers não substitui a execução dos testes após a migração.

- Etapa 15 — Testes automatizados: testes unitários, integração, propriedades, regressão e fuzzing estão registrados. Os resultados de CI anteriores são históricos; a suíte deve ser reexecutada localmente após a migração.

- Etapa 16 — Documentação e experimentos: protocolo reproduzível, manifesto JSON, proveniência/licenças/hashes de corpus, critérios de análise, estrutura para ablações e escala de geração e índice de resultados implementados. O smoke test sintético anterior está documentado com resultado negativo da G1 frente ao baseline unigram. Campanha real/licenciada e medição em hardware físico de 4 GB permanecem pendentes. Consulte [Etapa 16 — Documentação e experimentos](docs/49-etapa-16-documentacao-experimentos.md).
- Controles complementares de segurança e integração: validação de entrada/saída, identidade vinculada à sessão, revogação de grants e confirmações no encerramento, política de negação por padrão, checkpoint com CRC32 e escrita atômica em arquivo privado, logs sem conteúdo sensível e teto Linux de espaço virtual do servidor. Este trabalho complementar não altera a numeração do roteiro oficial; a etapa 18 é IA-LEARN. Consulte [Controles complementares de segurança e integração](docs/50-etapa-18-seguranca-integracao.md).

- Etapa 19 — Testes de aceitação: os fluxos de aceitação da G1 e da integração do Core estão registrados. A execução após a migração ainda está pendente.

- Etapa 20 — Primeiro treinamento completo da G1: runner reproduzível, scripts de treino, retomada, checkpoint de inferência, geração UTF-8, manifesto e hashes estão implementados. O workflow `G1` executa build, testes e o primeiro treinamento com retomada. Corpus natural/licenciado e medição em hardware físico de 4 GB permanecem pendentes.

- Etapa 12 — IA-TOOLS: registro, descoberta, esquema tipado, allowlist, grants de capacidade, confirmação de efeitos colaterais, validação UTF-8 e de tipos, limites de concorrência/entrada/saída/tempo, captura de exceções, auditoria injetável e lifecycle IA-CORE implementados. Teste `tools.tool_subsystem` registrado no CTest. Isolamento de processo/OS e interrupção forçada de handlers não fazem parte desta versão; handlers devem ser código confiável. Consulte [Etapa 12 — IA-TOOLS](docs/45-etapa-12-ia-tools.md).

- Etapa 1: infraestrutura CMake/C++20, presets, sanitizers, teste de fumaça e CI adicionados.
- Etapa 2: tipos comuns, validação UTF-8, IDs fortes, erros tipados e testes adicionados.
- Etapa 3: primitivas matemáticas estáveis e testes numéricos adicionados.
- Etapa 4: inventário tipado de 1.000 parâmetros FP32, ordem canônica e inicialização determinística.
- Etapa 5: forward pass fast/slow, cabeça binária causal, scoring em nats e testes adicionados.
- Etapa 6: BPTT truncado e gradientes manuais da G1 implementados.
- Etapa 7: AdamW, SGD com momentum, clipping global e testes transacionais implementados.
- Etapa 8: leitura/validação de corpus em fluxo, deduplicação exata, split determinístico por documento, treino BPTT em blocos, avaliação BPB, early stopping e CLI implementados.
- Etapa 9: formato MOROKCKP v1, CRC32, persistência de parâmetros/otimizador/RNG/cursor, fsync de arquivo e diretório, salvamento ao final de cada época, validação de compatibilidade, retomada determinística, logs JSONL append-only e exportação do melhor modelo de inferência integrados ao treinador.
- Etapa 10: executável `morok_benchmark`, baseline categórico softmax-256 unigram, matriz de comparações controladas, métricas BPB/throughput/tempo/RSS, JSONL e testes implementados. O fixture sintético valida o pipeline; avaliação linguística real aguarda corpus licenciado.
- Etapa 11: API de geração autoregressiva, leitura de checkpoint de inferência, prompt UTF-8 estrito, decodificação greedy limitada por bytes, preservação de UTF-8 e CLI `morok_generate` implementados; qualidade linguística não validada.
- Etapa 12: biblioteca de avaliação com estatísticas globais e por grupo dos 1.000 parâmetros, perplexidade por byte, normas médias/máximas de gradientes por época e relatório JSONL ampliado implementados; corpus real, baseline neural comparável e ablações arquiteturais continuam pendentes.
- Etapa 13: IA-CORE possui contratos C++20 para configuração validada, lifecycle, inicialização/rollback/encerramento, RequestContext, registro limitado de sessões, cancelamento atômico cooperativo, limites e health report; teste unitário registrado no CTest.
- Etapa 14: IA-MEM usa SQLite/WAL, FTS5 quando disponível, migração versionada, memória de trabalho/episódica/semântica, promoção explícita, proveniência, isolamento por SessionId, busca limitada por tempo/contexto, retenção, auditoria de exclusão, backup e integrity_check; teste `memory.store` registrado no CTest. O adaptador `MemorySubsystem` integra configuração IA-CORE e limpeza de memória de trabalho ao fechar sessão; o composition root ainda deve registrar os callbacks.
- Etapa 15: IA-LANG fornece `LanguageSubsystem`, valida os parâmetros G1, integra callbacks ao Runtime, cria/fecha sessões no `SessionRegistry`, mantém contexto UTF-8 isolado, aplica limites de entrada/contexto/geração, suporta reset e chama o decoder autoregressivo G1. O teste `lang.language_subsystem` está registrado no CTest. As asserções dos testes permanecem ativas também em builds Release. Identificação de idioma, tokenizer, streaming e persistência do contexto não fazem parte desta versão.
- Etapa 16: IA-COMM fornece criação/fechamento de conversas integradas às sessões IA-LANG/IA-CORE, RequestContext por conversa, mensagens system/user/assistant/tool, MessageId/ConversationId tipados, histórico ordenado com limites, correlação por RequestId, erros formatados e eventos de streaming com fragmentos UTF-8. O teste `comm.communication_subsystem` está registrado no CTest. O streaming é fragmentação após geração completa; histórico é volátil.
- Etapa 17: IA-REAS fornece tarefas estruturadas, contexto autorizado explícito, operações determinísticas matemáticas/lógicas, dependências declaradas e inferidas, ordenação topológica estável, detecção de ciclos, orçamento de 32 etapas, profundidade máxima 8, pré/pós-condições, fallback único, políticas abort_plan/continue_independent, cancelamento e vínculo de plano a SessionId/RequestId. O executor revalida a estrutura mutável do plano antes de executar, incluindo produtores, assinaturas, dependências, ordem topológica e profundidade; os tetos de 4 KiB por valor, 512 bytes por objetivo, 256 valores, 64 condições por escopo e 32 dependências explícitas por etapa não podem ser ampliados pela configuração. Testes cobrem planos adulterados, dependências desconhecidas, ordem inválida, assinaturas e limites. O teste `reason.reasoning_subsystem` está registrado no CTest. Não interpreta prompts livres, não executa ferramentas e não afirma raciocínio geral.
- Etapa 18: IA-LEARN fornece `LearningSubsystem`, lifecycle IA-CORE, política explícita para habilitar treinamento, limites de experimentos, execução serializada, treinamento sobre cópia privada dos parâmetros, validação do candidato, estatísticas e relatório com RequestId. O teste `learn.learning_subsystem` está registrado no CTest. Não há treinamento online nem promoção automática; cancelamento cooperativo é consultado durante varredura, documentos, blocos BPTT e avaliação.
- Protótipo experimental `morok_complete`: lê `entrada.txt`, treina a G1 do zero sobre a sequência, gera uma continuação ASCII e grava `saida.txt`. O fluxo é manual; não há execução automática por GitHub Actions.
- G1: inventário de parâmetros, forward pass, gradientes B0 e otimizadores implementados; os 29 parâmetros do segmentador permanecem fora do caminho preditivo.

## Próximas etapas

1. Executar campanha de benchmark com corpus real/licenciado e múltiplas sementes em hardware de referência.
2. Medir inferência e memória em hardware físico de referência com limite de 4 GB.
3. Executar ablações controladas e experimentos de escala de geração, registrando resultados e limitações.
4. Implementar segmentação reversível e integrar os subsistemas gradualmente.

## Princípio de engenharia

Toda hipótese deve ser testável. Uma otimização só será considerada válida quando comparada com uma referência simples, usando dados, hardware e condições equivalentes.


## Aprendizado por reforço

- **Reinforcement-G1** ([script](Gerações/G1/scripts/reinforcement.py), [histórico](Gerações/G1/data/reinforcement/answers.json)): atualiza os 1.000 parâmetros da G1, com checkpoint e artefatos próprios. Usa três perguntas controladas, avaliador lexical e gradiente reward-weighted baseado em BPTT; isso não demonstra compreensão semântica geral. Consulte [Experimento G1](docs/54-aprendizado-por-reforco-g1.md).
Os scripts de reforço são independentes e ficam dentro de suas respectivas gerações. Os workflows ativos estão em `.github/workflows/` na raiz do repositório.
