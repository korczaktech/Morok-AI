# 24. Estrutura completa do repositório

**Status:** proposta estrutural normativa para a evolução do Morok AI da G1 à G30.  
**Linguagem principal:** C++20 inicialmente, com evolução controlada do padrão conforme necessidade real.  
**Princípio:** uma única base de código, com componentes substituíveis e contratos estáveis; gerações são configurações/arquiteturas versionadas, não cópias do projeto.

## 1. Escala prevista

A sequência aprovada repete os multiplicadores 1, 2 e 5 em cada ordem de grandeza:

| Geração | Parâmetros treináveis |
|---|---:|
| G1–G3 | 1 mil, 2 mil, 5 mil |
| G4–G6 | 10 mil, 20 mil, 50 mil |
| G7–G9 | 100 mil, 200 mil, 500 mil |
| G10–G12 | 1 milhão, 2 milhões, 5 milhões |
| G13–G15 | 10 milhões, 20 milhões, 50 milhões |
| G16–G18 | 100 milhões, 200 milhões, 500 milhões |
| G19–G21 | 1 bilhão, 2 bilhões, 5 bilhões |
| G22–G24 | 10 bilhões, 20 bilhões, 50 bilhões |
| G25–G27 | 100 bilhões, 200 bilhões, 500 bilhões |
| G28–G30 | 1 trilhão, 2 trilhões, 5 trilhões |

G30 representa 5 trilhões de parâmetros pela regra matemática atual. Isso é um alvo de escala de longo prazo, não uma promessa de que o hardware, os dados, o orçamento ou a arquitetura atual consigam treiná-lo. A estrutura prevê particionamento e execução distribuída, mas esses recursos só serão implementados quando uma geração exigir e houver hardware para validar.

## 2. Árvore proposta

A árvore abaixo representa a estrutura-alvo. Diretórios de infraestrutura avançada podem permanecer vazios ou conter apenas documentação até que sejam necessários. Não criar arquivos-fonte vazios só para preencher a árvore.

```text
Morok-AI/
├── .github/
│   ├── workflows/
│   │   ├── ci.yml
│   │   ├── sanitizers.yml
│   │   ├── fuzzing.yml
│   │   ├── reproducibility.yml
│   │   └── release.yml
│   ├── ISSUE_TEMPLATE/
│   │   ├── bug.yml
│   │   └── experiment.yml
│   └── PULL_REQUEST_TEMPLATE.md
│
├── CMakeLists.txt
├── CMakePresets.json
├── CMakeUserPresets.example.json
├── CMake/
│   ├── MorokCompilerOptions.cmake
│   ├── MorokDependencies.cmake
│   ├── MorokTargets.cmake
│   ├── MorokSanitizers.cmake
│   └── MorokPackaging.cmake
├── LICENSE
├── NOTICE
├── README.md
├── CONTRIBUTING.md
├── SECURITY.md
├── CODE_OF_CONDUCT.md
├── CHANGELOG.md
├── VERSION
│
├── docs/
│   ├── 01-visao-geral.md
│   ├── 02-arquitetura-geral.md
│   ├── 03-subsistemas.md
│   ├── 04-modelo-e-parametros.md
│   ├── 05-rlah-visao-geral.md
│   ├── 06-rlah-objetivos-e-agrupamentos.md
│   ├── 07-rlah-segmentacao-e-hierarquia.md
│   ├── 08-contexto-e-predicao.md
│   ├── 09-treinamento-e-avaliacao.md
│   ├── 10-recursos-e-otimizacao.md
│   ├── 11-geracao-1.md
│   ├── 12-pendencias-e-validacao.md
│   ├── 13-arquitetura-matematica-rlah.md
│   ├── 14-predicao-binaria-e-objetivo.md
│   ├── 15-segmentacao-adaptativa-e-criterio.md
│   ├── 16-protocolo-de-treinamento.md
│   ├── 17-revisao-bibliografica-e-posicionamento.md
│   ├── 18-especificacao-operacional-g1.md
│   ├── 19-contratos-de-interfaces.md
│   ├── 20-subsistemas-cognitivos.md
│   ├── 21-persistencia-e-memoria.md
│   ├── 22-seguranca-e-permissoes.md
│   ├── 23-build-testes-e-distribuicao.md
│   ├── 24-estrutura-completa-repositorio.md
│   ├── architecture/
│   │   ├── decisions/
│   │   ├── diagrams/
│   │   └── compatibility/
│   ├── research/
│   │   ├── literature/
│   │   ├── hypotheses/
│   │   └── ablations/
│   ├── operations/
│   │   ├── training-runs/
│   │   ├── deployment/
│   │   └── incident-response/
│   └── specifications/
│       ├── formats/
│       ├── protocols/
│       └── generation-profiles/
│
├── include/
│   └── morok/
│       ├── common/
│       │   ├── types.hpp
│       │   ├── status.hpp
│       │   ├── result.hpp
│       │   ├── errors.hpp
│       │   ├── byte_buffer.hpp
│       │   ├── utf8_text.hpp
│       │   ├── identifiers.hpp
│       │   ├── timestamps.hpp
│       │   └── version.hpp
│       ├── core/
│       │   ├── runtime.hpp
│       │   ├── lifecycle.hpp
│       │   ├── configuration.hpp
│       │   ├── request_context.hpp
│       │   ├── cancellation.hpp
│       │   ├── resource_limits.hpp
│       │   └── health.hpp
│       ├── lang/
│       │   ├── byte_encoder.hpp
│       │   ├── utf8_validator.hpp
│       │   ├── rlah/
│       │   │   ├── representation.hpp
│       │   │   ├── segmenter.hpp
│       │   │   ├── candidate.hpp
│       │   │   ├── scorer.hpp
│       │   │   ├── adaptive_controller.hpp
│       │   │   └── reversible_codec.hpp
│       │   └── generation.hpp
│       ├── model/
│       │   ├── model.hpp
│       │   ├── model_config.hpp
│       │   ├── architecture_id.hpp
│       │   ├── parameter_registry.hpp
│       │   ├── parameter_view.hpp
│       │   ├── state.hpp
│       │   ├── tensor.hpp
│       │   ├── math/
│       │   │   ├── activations.hpp
│       │   │   ├── initialization.hpp
│       │   │   ├── numerics.hpp
│       │   │   └── kernels.hpp
│       │   ├── layers/
│       │   │   ├── recurrent_cell.hpp
│       │   │   ├── fast_state.hpp
│       │   │   ├── slow_state.hpp
│       │   │   └── prediction_head.hpp
│       │   └── backends/
│       │       ├── backend.hpp
│       │       ├── cpu_backend.hpp
│       │       ├── simd_backend.hpp
│       │       └── distributed_backend.hpp
│       ├── inference/
│       │   ├── engine.hpp
│       │   ├── session.hpp
│       │   ├── sampler.hpp
│       │   ├── decoding.hpp
│       │   ├── streaming.hpp
│       │   └── inference_limits.hpp
│       ├── learn/
│       │   ├── trainer.hpp
│       │   ├── training_config.hpp
│       │   ├── dataset.hpp
│       │   ├── data_loader.hpp
│       │   ├── batching.hpp
│       │   ├── optimizer.hpp
│       │   ├── adamw.hpp
│       │   ├── sgd_momentum.hpp
│       │   ├── gradients.hpp
│       │   ├── backpropagation.hpp
│       │   ├── bptt.hpp
│       │   ├── scheduler.hpp
│       │   ├── checkpoint.hpp
│       │   └── evaluation.hpp
│       ├── memory/
│       │   ├── memory_store.hpp
│       │   ├── working_memory.hpp
│       │   ├── episodic_memory.hpp
│       │   ├── semantic_memory.hpp
│       │   ├── provenance.hpp
│       │   ├── retrieval.hpp
│       │   └── retention_policy.hpp
│       ├── reason/
│       │   ├── planner.hpp
│       │   ├── task_graph.hpp
│       │   ├── execution_plan.hpp
│       │   ├── deterministic_ops.hpp
│       │   └── reasoning_limits.hpp
│       ├── tools/
│       │   ├── tool_registry.hpp
│       │   ├── tool_schema.hpp
│       │   ├── permission_policy.hpp
│       │   ├── confirmation.hpp
│       │   ├── sandbox.hpp
│       │   └── audit.hpp
│       ├── comm/
│       │   ├── message.hpp
│       │   ├── conversation.hpp
│       │   ├── session_manager.hpp
│       │   ├── formatter.hpp
│       │   └── event_stream.hpp
│       ├── storage/
│       │   ├── sqlite/
│       │   │   ├── connection.hpp
│       │   │   ├── migrations.hpp
│       │   │   └── repositories.hpp
│       │   ├── object_store.hpp
│       │   ├── content_hash.hpp
│       │   └── atomic_file.hpp
│       ├── distributed/
│       │   ├── topology.hpp
│       │   ├── process_group.hpp
│       │   ├── parameter_shard.hpp
│       │   ├── gradient_reduction.hpp
│       │   ├── checkpoint_shard.hpp
│       │   └── collective_ops.hpp
│       ├── observability/
│       │   ├── logger.hpp
│       │   ├── metrics.hpp
│       │   ├── tracing.hpp
│       │   └── experiment_report.hpp
│       └── api/
│           ├── morok.hpp
│           ├── model_api.hpp
│           ├── inference_api.hpp
│           └── training_api.hpp
│
├── src/
│   ├── common/
│   ├── core/
│   ├── lang/
│   │   └── rlah/
│   ├── model/
│   │   ├── math/
│   │   ├── layers/
│   │   └── backends/
│   ├── inference/
│   ├── learn/
│   │   ├── optimizers/
│   │   ├── data/
│   │   ├── gradients/
│   │   ├── checkpoint/
│   │   └── distributed/
│   ├── memory/
│   ├── reason/
│   ├── tools/
│   ├── comm/
│   ├── storage/
│   │   └── sqlite/
│   ├── distributed/
│   ├── observability/
│   └── api/
│
├── apps/
│   ├── morok-cli/
│   │   └── main.cpp
│   ├── morok-server/
│   │   └── main.cpp
│   ├── morok-train/
│   │   └── main.cpp
│   ├── morok-evaluate/
│   │   └── main.cpp
│   └── morok-convert/
│       └── main.cpp
│
├── configs/
│   ├── defaults.toml
│   ├── logging.toml
│   ├── security.toml
│   ├── memory.toml
│   ├── generations/
│   │   ├── g1.toml
│   │   ├── g2.toml
│   │   ├── g3.toml
│   │   └── README.md
│   ├── training/
│   ├── inference/
│   └── hardware/
│
├── tests/
│   ├── unit/
│   │   ├── common/
│   │   ├── core/
│   │   ├── lang/
│   │   ├── model/
│   │   ├── inference/
│   │   ├── learn/
│   │   ├── memory/
│   │   ├── reason/
│   │   ├── tools/
│   │   ├── comm/
│   │   ├── storage/
│   │   └── distributed/
│   ├── integration/
│   │   ├── model_training/
│   │   ├── inference_pipeline/
│   │   ├── rlah_pipeline/
│   │   ├── memory_pipeline/
│   │   └── subsystem_contracts/
│   ├── acceptance/
│   │   ├── g1/
│   │   ├── generation_profiles/
│   │   └── end_to_end/
│   ├── regression/
│   ├── property/
│   ├── fuzz/
│   │   ├── utf8_fuzz.cpp
│   │   ├── checkpoint_fuzz.cpp
│   │   └── rlah_codec_fuzz.cpp
│   ├── fixtures/
│   └── support/
│
├── benchmarks/
│   ├── micro/
│   ├── model/
│   ├── inference/
│   ├── training/
│   ├── memory/
│   ├── scaling/
│   ├── datasets/
│   └── README.md
│
├── tools/
│   ├── corpus/
│   │   ├── validate_corpus.py
│   │   ├── split_corpus.py
│   │   └── hash_corpus.py
│   ├── checkpoints/
│   │   ├── inspect_checkpoint.py
│   │   └── verify_checkpoint.py
│   ├── experiments/
│   │   ├── compare_runs.py
│   │   └── summarize_metrics.py
│   ├── profiling/
│   └── development/
│
├── scripts/
│   ├── configure.sh
│   ├── build.sh
│   ├── test.sh
│   ├── benchmark.sh
│   ├── format.sh
│   └── verify_reproducibility.sh
│
├── data/
│   ├── README.md
│   ├── raw/
│   │   └── .gitkeep
│   ├── interim/
│   │   └── .gitkeep
│   ├── processed/
│   │   └── .gitkeep
│   └── manifests/
│
├── artifacts/
│   ├── README.md
│   ├── checkpoints/
│   │   └── .gitkeep
│   ├── tokenization/
│   ├── reports/
│   └── exports/
│
├── experiments/
│   ├── templates/
│   ├── ablations/
│   ├── generation-scaling/
│   └── results/
│       └── .gitkeep
│
├── third_party/
│   └── README.md
│
└── packaging/
    ├── linux/
    ├── containers/
    └── systemd/
```

## 3. Responsabilidade dos blocos

- **common:** tipos e contratos compartilhados. Não contém lógica de modelo.
- **core / IA-CORE:** ciclo de vida, configuração, limites, identidade e coordenação.
- **lang / IA-LANG:** UTF-8, bytes e RLAH. A representação não deve depender de uma geração específica.
- **model:** arquitetura neural, parâmetros, estados, operações matemáticas e backends.
- **inference:** execução de modelos já carregados; não treina nem acessa diretamente o banco de memória.
- **learn / IA-LEARN:** corpus, treinamento, gradientes, otimizadores, avaliação e checkpoints.
- **memory / IA-MEM:** memória de trabalho, episódica e semântica, com proveniência.
- **reason / IA-REAS:** planejamento e operações determinísticas; não deve fingir que texto gerado é prova de raciocínio correto.
- **tools / IA-TOOLS:** ferramentas com permissões explícitas, isolamento, limites e auditoria.
- **comm / IA-COMM:** conversas, sessões, mensagens e streaming.
- **storage:** persistência técnica. Não define a política cognitiva da memória.
- **distributed:** paralelismo entre processos/máquinas, shards e operações coletivas. Não é dependência da G1.
- **observability:** métricas, logs, tracing e relatórios de experimentos.
- **apps:** executáveis que usam as bibliotecas; regras de negócio não devem ficar nos `main.cpp`.
- **configs/generations:** perfis versionados de arquitetura e orçamento de parâmetros. Não duplicar código por geração.

## 4. Regras para G1 até G30

1. Cada geração é descrita por um perfil de configuração e um identificador de arquitetura versionado.
2. O inventário de parâmetros treináveis deve ser calculado automaticamente e testado; não confiar em números escritos manualmente.
3. O total inclui todos os parâmetros treináveis de entrada, núcleo, cabeças, adaptadores e componentes auxiliares que sejam treináveis.
4. Componentes sem parâmetros treináveis devem ser contabilizados separadamente, sem serem somados ao total.
5. Checkpoints registram arquitetura, formato, precisão, partições, otimizador, corpus, estado RNG e progresso.
6. Formatos de checkpoint devem suportar shards e manifesto global antes de a primeira geração distribuída ser adotada.
7. A execução em CPU é a referência inicial. SIMD, GPU e distribuição são backends opcionais, validados contra a referência numérica.
8. Não introduzir dependências de GPU, rede distribuída ou frameworks pesados na G1.
9. A arquitetura pode mudar entre gerações, mas migrações de checkpoint só são permitidas por conversores explícitos e testados.
10. Nenhuma geração é considerada concluída apenas por atingir a contagem de parâmetros: exige treinamento reproduzível, avaliação, benchmark, segurança e documentação de limitações.

## 5. Evolução por capacidade

- **G1–G3:** CPU, baixo uso de memória, execução local, treinamento simples e checkpoints monolíticos.
- **G4–G9:** melhorar kernels, batching, carregamento de dados, profiling e formatos de checkpoint; paralelismo local somente se medido como vantajoso.
- **G10–G18:** preparar execução acelerada, precisão mista validada, checkpoints particionados e treinamento multi-dispositivo.
- **G19–G24:** paralelismo de dados/modelo, comunicação coletiva, recuperação de falhas e armazenamento distribuído tornam-se requisitos possíveis, condicionados a hardware e resultados.
- **G25–G30:** sharding hierárquico, planejamento de memória/energia, treinamento distribuído em larga escala e operação de cluster. A viabilidade deverá ser reavaliada; não se presume que a arquitetura G1 escale sem alterações.

## 6. Limites desta árvore

Esta é a arquitetura-alvo do repositório, não uma afirmação de que todos os arquivos ou subsistemas já existem. A implementação deve ser incremental: criar primeiro os diretórios e arquivos necessários à G1, mantendo contratos que não bloqueiem as gerações futuras. Pastas de recursos futuros só devem receber código quando houver requisito, hardware e testes correspondentes.

Dados de treinamento, checkpoints grandes, credenciais, logs sensíveis e artefatos privados não devem ser versionados no Git. O repositório deve conter manifests, instruções e pequenos fixtures reproduzíveis, não corpora ou pesos completos.
