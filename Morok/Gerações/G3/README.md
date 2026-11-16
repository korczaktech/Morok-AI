# Morok G3

A G3 é a geração-base da linhagem Morok. Ela estende o motor Morok existente,
sem criar outro Core ou runtime. A infraestrutura neutra continua em
`Morok::Core`; o contrato de execução continua em
`morok::model::ModelBackend`.

## Arquitetura fixa

- 261 IDs byte-level.
- Embedding 60, GRU 97 e GRU 68, duas camadas.
- RMSNorm entre as camadas.
- Projeção 68 → 60 e weight tying real.
- Exatamente 100.000 parâmetros.
- Estado recorrente FP32 de 660 bytes por sessão.
- ID de arquitetura `0x0302`.

A contagem é verificada a partir dos tensores reais. Consulte
[PARAMETROS-100K.md](PARAMETROS-100K.md).

## Componentes

| Módulo | Biblioteca/CLI | Responsabilidade |
|---|---|---|
| `neural/` | `Morok::G3::Neural` | Backend GRU e kernels |
| `training/` | `Morok::G3::Training`, `morok-g3-train` | Dataset, SFT, BPTT, AdamW, checkpoint e retomada |
| `rl/` | `Morok::G3::RL`, `morok-g3-reinforce` | REINFORCE, baseline, KL e avaliador independente |
| `inference/` | `Morok::G3::Inference`, `morok-g3-generate` | Geração, sessões, artefatos, INT8 e benchmark |
| `profiles/` | `Morok::G3::Profiles` | Políticas dos quatro perfis sobre a mesma execução |

A quantização INT8 mantém as matrizes quantizadas no caminho de inferência.
Biases, escalas, RMSNorm, ativações e estado recorrente permanecem FP32.

## Perfis

Mokosh, Tsukuyomi, Artemis e Apollo usam o mesmo backend G3 e o mesmo runtime.
As diferenças são políticas de geração, limites e quantidade de agentes. Os
perfis não possuem pesos próprios. Consulte [profiles/README.md](profiles/README.md).

## Treinamento e retomada

O SFT divide grupos 85/10/5, aplica expansão/perturbação apenas no treino e
usa Cross-Entropy causal, TBPTT e AdamW. Checkpoints de época persistem pesos,
momentos AdamW, contador, scheduler, sampler, RNG, fingerprints e metadados.
A retomada valida compatibilidade e continua no limite entre épocas. Consulte
[training/README.md](training/README.md).

## Inferência e benchmarks

O CLI carrega artefatos FP32 ou INT8, aceita Top-K/temperatura, protege contra
repetição e mantém estado e RNG isolados por sessão. Benchmark mede mediana,
P95, throughput, RSS e pico de RSS no Linux. Consulte
[inference/INFERENCE.md](inference/INFERENCE.md).

## Aprovação

A validação de arquitetura, treinamento, retomada, RL, checkpoints, inferência,
quantização, isolamento de sessões e perfis está automatizada em CTest e GitHub
Actions. O relatório consolidado é
[RELATORIO-ETAPA-9-APROVACAO-PERFIS.md](RELATORIO-ETAPA-9-APROVACAO-PERFIS.md).

Os testes de CI usam fixtures sintéticas. Eles verificam implementação e
reprodutibilidade do pipeline, mas não demonstram qualidade linguística,
raciocínio geral ou superioridade semântica. A aprovação de qualidade exige
dataset de produção revisado e avaliação humana/independente.
