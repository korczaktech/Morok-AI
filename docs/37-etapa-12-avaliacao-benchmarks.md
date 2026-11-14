# Etapa 12 — Avaliação, métricas e benchmarks da G1

## Objetivo

Transformar as métricas já existentes da Etapa 10 em um relatório de avaliação mais completo, reprodutível e auditável. Esta etapa adiciona diagnósticos numéricos de gradientes e parâmetros, perplexidade por byte, comparação quantitativa com o baseline existente e testes para as fórmulas.

## Implementação entregue

### Biblioteca de avaliação

- `include/morok/model/learn/g1_evaluation.hpp`
- `src/model/learn/g1_evaluation.cpp`
- `tests/unit/model/test_g1_evaluation.cpp`

A API oferece:
- `summarize_g1_values`: contagem, zeros, mínimo, máximo, média e desvio-padrão populacional via algoritmo online de Welford; rejeita faixa vazia, ponteiro nulo e valores não finitos.
- `evaluate_g1_parameters`: valida e resume o inventário completo de 1.000 parâmetros e os grupos canônicos:
  - estado rápido: 506;
  - estado lento: 253;
  - cabeça de predição: 212;
  - pontuador/segmentador: 29.
- `summarize_g1_loss`: transforma NLL acumulada e número de alvos em nats/byte, bits/byte (BPB) e perplexidade por byte (2^{BPB}); rejeita contagem zero, perda negativa ou valores não finitos.

A perplexidade reportada é perplexidade por byte, não perplexidade por palavra ou token linguístico. O inventário arquitetural contém 1.000 parâmetros; no estado atual do treinamento B0, somente 971 recebem gradientes/updates e os 29 parâmetros do segmentador permanecem fora do caminho de treino.

### Diagnóstico de treinamento

Cada época agora registra:
- norma de gradiente da última atualização;
- média da norma pré-clipping em todas as atualizações da época;
- maior norma pré-clipping observada;
- tempo de parede por época, incluindo treino e avaliação de validação;
- BPB de treino e validação;
- perplexidade por byte de treino e validação.

Os valores são gravados no JSONL persistente da execução. A média de norma é ponderada igualmente por atualização, não por documento. O tempo por época inclui processamento de treino e avaliação de validação.

### Relatório de benchmark

O executável `morok_benchmark` acrescenta ao JSONL:
- contagem de parâmetros da arquitetura (1.000), contagem atualizada pelo caminho preditivo B0 (971) e verificação de finitude; os 29 parâmetros do segmentador permanecem reservados e não são atualizados pelo B0;
- média, desvio-padrão, mínimo e máximo da distribuição global;
- média e desvio-padrão por grupo rápido, lento, predição e segmentador;
- perplexidade por byte de treino, validação e teste;
- norma média ponderada e norma máxima de gradiente; tempo médio e máximo por época;
- diferença absoluta e razão de BPB entre G1 e baseline unigram.

Continuam registrados commit, compilador, máquina, CPU, hash, origem/licença declaradas, semente, divisão, alvos, épocas, atualizações, BPB, throughput, latência de avaliação, tempo total, pico RSS e early stopping.

### Matriz controlada mantida

Por semente, a campanha executa seis configurações:
1. AdamW, LR 0,001, BPTT 32;
2. AdamW, LR 0,001, BPTT 64 (referência);
3. AdamW, LR 0,001, BPTT 128;
4. SGD Momentum, LR 0,01, BPTT 64;
5. AdamW, LR 0,0003, BPTT 64;
6. AdamW, LR 0,003, BPTT 64.

As comparações alteram uma dimensão por vez. Repetições usam sementes explícitas. O conjunto de teste permanece fora da seleção de hiperparâmetros.

## Interpretação e limites

- Baseline atual: softmax-256 unigram com suavização add-one. Não possui contexto e não é uma rede neural de capacidade equivalente.
- Baseline softmax-256 neural condicionado ao mesmo estado recorrente: ainda pendente.
- A matriz atual compara BPTT, otimizador e learning rate; não executa automaticamente todas as ablações de arquitetura previstas.
- Comparação fast/slow contra RNN simples, segmentação ligada/desligada, hierarquia de escalas e pesos adaptativos/fixos continuam pendentes. O segmentador de 29 parâmetros não participa do caminho preditivo B0, portanto não existe resultado end-to-end legítimo para atribuir a ele.
- O conjunto de fixtures sintéticos em CI verifica o mecanismo e as fórmulas, não aprendizagem de linguagem.
- Ainda é necessário corpus real/licenciado com origem, licença e hash verificáveis, múltiplas sementes e execução no hardware de referência de 4 GB.
- O pico RSS é máximo cumulativo do processo da campanha; não é uma medição isolada por configuração nem prova de consumo total do dispositivo.
- A perplexidade por byte é uma transformação de BPB e não uma medida independente de qualidade.

Nenhuma conclusão de superioridade da G1 deve ser tirada sem baseline neural comparável, corpus representativo e repetições controladas.

## Comandos

Build e testes:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Campanha com corpus licenciado:

```sh
mkdir -p benchmark-results
./build/morok_benchmark ./data/corpus benchmark-results/g1.jsonl 10 42 "LICENCA-REAL" "ORIGEM-REAL" 3
```

A licença e a origem devem descrever exatamente os dados utilizados. Use um arquivo de saída novo para cada campanha.


## Smoke test executado no CI

A matriz de seis configurações foi executada no commit `94c6cb40f5e5bf56d14900106c363f871aea29fb`, com uma época, seed 42 e fixture sintético CC0-1.0:

- 6 documentos únicos, 559 bytes; 398 alvos de treino por época e 86 alvos de teste.
- GCC 13.3.0, Linux x86-64, Intel Xeon 6975P-C (runner de CI).
- G1: BPB de teste entre 7,9658 e 8,8871; perplexidade por byte entre 250,0 e 473,5.
- Baseline softmax-256 unigram: 4,9958 BPB em todas as configurações.
- Delta de G1 contra o baseline: +2,9700 a +3,8913 BPB; o baseline venceu nas seis configurações.
- Normas médias pré-clipping: 2,3850 a 2,7250; máximas: 2,9841 a 4,1492.
- Desvio-padrão global dos parâmetros após treino: aproximadamente 0,2943 a 0,2951; os 29 parâmetros do segmentador permaneceram em zero, pois ainda não participam do B0.
- Throughput efetivo observado: aproximadamente 282 mil a 426 mil alvos/s.
- Latência de avaliação: aproximadamente 896 a 934 ns por byte-alvo.
- Tempo médio por época: aproximadamente 0,00082 a 0,00127 segundo no fixture. Pico RSS: 4.935.680 bytes na campanha.

O resultado confirma que o relatório novo contém métricas finitas e os grupos de parâmetros esperados. Também repete o resultado negativo: no fixture, o baseline unigram superou a G1 em todas as configurações. Uma época e seis documentos sintéticos não permitem inferir qualidade de linguagem ou escolher hiperparâmetros para uso real.

Artefato JSONL: [g1-benchmark-smoke — execução 43531723944](https://github.com/korczaktech/Morok-AI/actions/runs/43531723944/artifacts/14088325005). O JSONL registra 1.000 parâmetros arquiteturais, 971 atualizados pelo B0 e segmentador não atualizado.

Validação do código no mesmo commit:
- [CI GCC/Clang Debug/Release — sucesso](https://github.com/korczaktech/Morok-AI/actions/runs/43531723944).
- [ASan/UBSan — sucesso](https://github.com/korczaktech/Morok-AI/actions/runs/43531723952).

## Critérios de aceitação da implementação

- Estatísticas numéricas determinísticas e rejeição de NaN/Inf.
- Inventário de parâmetros dividido nos grupos 506/253/212/29 e total exato 1.000.
- BPB e perplexidade por byte matematicamente consistentes.
- Normas de gradiente agregadas em cada época e registradas no JSONL.
- Relatório de benchmark inclui parâmetros, gradientes, perplexidade e comparação de BPB.
- Testes unitários registrados no CTest.
- CI GCC/Clang Debug/Release e ASan/UBSan aprovados.

## Estado final

A instrumentação de avaliação e o relatório ampliado estão implementados. A validação científica completa permanece pendente por falta de corpus real/licenciado, baseline neural comparável, ablações arquiteturais e execução em hardware de referência. Isso é uma dependência de evidência e dados, não um teste que possa ser substituído por fixture sintético.
