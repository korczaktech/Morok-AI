# Experimentos do Morok AI

Esta pasta contém protocolos, manifestos e referências a resultados. Não contém corpus de treinamento, dados pessoais, checkpoints privados ou resultados inventados.

## Fluxo recomendado

1. Escolha uma pergunta mensurável e defina hipótese, controles e critérios antes de executar.
2. Copie `templates/manifest.example.json` para um novo manifesto com identificador único.
3. Preencha proveniência, licença, SHA-256 dos dados, splits, commit, ambiente, sementes e configuração.
4. Execute os testes e o benchmark correspondente em build Release.
5. Salve a saída JSONL em um arquivo novo, sem sobrescrever campanhas anteriores.
6. Atualize o manifesto com status, horários, códigos de saída, hashes dos artefatos e resultados.
7. Registre interpretação, limitações e desvios do protocolo em `results/`.

## Benchmark G1

Build e testes:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Campanha:

```sh
mkdir -p experiment-output
./build/morok_benchmark ./data/corpus experiment-output/g1-<id>.jsonl 10 42 "LICENCA-REAL" "ORIGEM-REAL" 3
```

Substitua os placeholders por valores verdadeiros. O executável acrescenta linhas ao arquivo; use um caminho novo por campanha. O corpus precisa conter documentos UTF-8 válidos e ter licença de uso compatível.

O benchmark atual mede seis configurações controladas por semente. Consulte `docs/35-etapa-10-benchmark-validacao.md` para as definições, métricas, baseline e limitações.

## Regras de integridade

- Não executar treinamento no conjunto de teste.
- Não usar dados sem origem/licença verificável.
- Não registrar segredos, dados pessoais ou conteúdo privado nos manifestos.
- Não comparar métricas com splits, versões ou ambientes diferentes sem declarar a diferença.
- Preservar resultados negativos, falhas e execuções incompletas.
- Não tratar o benchmark sintético de CI como validação de linguagem natural.
- Não afirmar limite de RAM de 4 GB sem medição em hardware com esse limite.

## Índices

- `ablations/README.md`: desenho de ablações.
- `generation-scaling/README.md`: medições de geração.
- `results/README.md`: registro de resultados.
- `templates/manifest.example.json`: manifesto inicial.
