# Etapa 10 — Benchmark e validação experimental da G1

## Objetivo

Estabelecer um protocolo reproduzível para comparar a G1 com um baseline categórico de 256 classes e medir custo de treinamento. A etapa separa claramente código implementado, execução de smoke test com dados sintéticos e validação científica com corpus real.

## Matriz de experimentos

O executável `morok_benchmark` executa seis configurações por semente:

| Dimensão | Valores |
|---|---|
| BPTT | 32, 64 e 128 bytes |
| Otimizador | AdamW e SGD Momentum |
| Sementes | `seed` até `seed + repetitions - 1` |
| Inicialização | mesma semente para cada comparação correspondente |
| Épocas | limite informado pelo operador |
| Dados | mesmos documentos e split para cada configuração com a mesma semente |

A validação escolhe o melhor checkpoint durante o treino. O conjunto de teste é avaliado somente depois dessa seleção. Não ajustar hiperparâmetros com base no teste.

## Baseline softmax-256

Foi implementado um baseline categórico de 256 classes com logits constantes, equivalentes às frequências empíricas dos bytes-alvo do conjunto de treino. Usa suavização add-one:

`p(b) = (count_train(b) + 1) / (N_train + 256)`

O baseline prevê cada byte-alvo sem contexto. É um baseline de frequência/softmax-256 simples, não uma rede neural com capacidade comparável à G1. A diferença de BPB é diagnóstica e não prova superioridade geral.

A unidade de alvo segue a G1: o primeiro byte de cada documento é contexto e os bytes seguintes são alvos. Split, ordenação lexicográfica e deduplicação exata são reproduzidos para manter a comparação alinhada.

## Métricas registradas

Cada linha JSONL registra:
- commit, compilador, sistema, arquitetura e CPU;
- origem declarada, licença declarada e hash do corpus;
- semente, quantidade de documentos e bytes;
- BPTT e otimizador;
- épocas concluídas, atualizações e quantidade de alvos;
- BPB de validação e teste da G1;
- BPB de teste do baseline softmax-256 unigram;
- throughput de treinamento em bytes-alvo por segundo;
- tempo total de parede e pico de RSS do processo;
- indicação de early stopping.

O throughput é calculado por alvos de treino processados dividido pelo tempo total de treinamento, avaliação e overhead medido da chamada. Não deve ser interpretado como throughput puro de kernel. RSS é o pico reportado pelo sistema operacional para o processo, não a RAM do dispositivo inteiro.

## Uso local

Compile:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Execute a matriz com um corpus próprio:

```sh
mkdir -p benchmark-results
./build/morok_benchmark ./data/corpus benchmark-results/g1.jsonl 10 42 "CC-BY-4.0" "URL-ou-identificador-da-origem" 3
```

O operador deve informar a licença e a origem reais do corpus. Não declarar uma licença que não se aplica. O programa grava em append; use um arquivo novo para uma campanha nova.

Os argumentos finais são: épocas, semente inicial, licença, origem e número de repetições. Cada repetição incrementa a semente. O limite de repetições é 20.

## GitHub Actions

O workflow manual `.github/workflows/g1-benchmark.yml` compila o benchmark, executa os testes do baseline, roda a matriz com o corpus sintético versionado e publica `g1.jsonl` como artifact.

O corpus sintético foi criado para testar a mecânica do pipeline e está identificado como CC0-1.0. Ele não representa linguagem natural suficiente para avaliar qualidade linguística, generalização ou eficácia da G1.

Para executar: GitHub → Actions → G1 Benchmark → Run workflow. Os parâmetros permitem ajustar épocas e repetições.

## Critérios de aceitação da implementação

- Baseline calcula BPB finito e determinístico.
- Deduplicação exata e divisão por documento seguem a política da G1.
- Matriz de seis configurações é executável.
- Repetições usam sementes explícitas e registradas.
- JSONL inclui proveniência e métricas de custo/qualidade.
- Benchmark compila e testes unitários passam em GCC e Clang.
- Workflow publica o relatório como artifact.

## Limites e validação ainda necessária

- O baseline softmax-256 é unigram/sem contexto e não é uma comparação de capacidade equivalente.
- O benchmark de throughput inclui overhead de treino e avaliação; uma medição de kernel isolado poderá ser adicionada depois.
- Pico de RSS em uma única execução não representa o consumo de memória em todos os dispositivos.
- Os fixtures sintéticos validam o pipeline, não a aprendizagem de linguagem.
- Nenhum corpus real/licenciado foi fornecido nesta etapa. Portanto, BPB real, qualidade linguística, eficácia, estabilidade em múltiplas sementes e meta de RSS em hardware de 4 GB continuam sem validação.
- Não declarar que a G1 vence o baseline até comparar resultados em corpus de teste representativo, com múltiplas sementes e sem vazamento de dados.

## Estado

A infraestrutura de benchmark, baseline, testes e workflow automatizado está implementada. A validação experimental com corpus real depende da disponibilização de dados com licença e proveniência verificáveis e da execução da campanha no hardware de referência.
