# Etapa 15 — Testes automatizados

**Sequência:** roteiro de 20 etapas do projeto. Esta etapa amplia a validação automatizada; não altera a numeração operacional interna da G1.

## Objetivo

Estabelecer uma suíte automatizada em camadas que valide contratos locais, integração entre subsistemas, invariantes matemáticos, regressões e robustez diante de entradas malformadas. Build bem-sucedido, isoladamente, não é critério de aprovação.

## Camadas implementadas

| Camada | Localização | Responsabilidade |
|---|---|---|
| Unitária | \`tests/unit\` | Contratos de funções, classes e subsistemas |
| Integração | \`tests/integration\` | Fluxos completos entre subsistemas e artefatos |
| Propriedades | \`tests/property\` | Invariantes sobre conjuntos determinísticos de entradas |
| Regressão | \`tests/regression\` | Falhas históricas e rollback de lifecycle |
| Fuzzing | \`tests/fuzz\` | Entradas arbitrárias para validadores e parsers |
| Aceitação | \`tests/acceptance\` | Cenários de comportamento do produto e G1 |
| Fixtures | \`tests/fixtures\` | Dados pequenos, determinísticos e versionados |
| Suporte | \`tests/support\` | Infraestrutura compartilhada de testes |

## Cobertura adicionada nesta etapa

- Testes de propriedades para round-trip do inventário de 1.000 parâmetros, validação UTF-8 de bytes individuais, codificação de features de byte, estabilidade numérica do estado recorrente e round-trip do codec RLAH sobre dados pseudoaleatórios com semente fixa.
- Teste de integração treinamento → checkpoint → carregamento → geração, usando corpus sintético versionado, semente fixa e diretório temporário isolado.
- Teste de regressão do rollback do IA-CORE: inicializadores concluídos são encerrados em ordem inversa quando um inicializador posterior falha.
- Harnesses libFuzzer para validação UTF-8, decoder RLAH e parser de checkpoint. Os harnesses aceitam dados arbitrários e tratam rejeição de entrada malformada como resultado esperado.
- Timeouts CTest para evitar jobs presos indefinidamente.
- Execução contínua das novas camadas no CI principal e workflow dedicado de fuzzing com Clang, ASan e UBSan.

## Comandos

Testes normais:

\`\`\`sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DMOROK_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
ctest --test-dir build -L integration --output-on-failure
ctest --test-dir build -L property --output-on-failure
\`\`\`

Fuzzing (Clang/libFuzzer):

\`\`\`sh
CXX=clang++ cmake -S . -B build-fuzz -G Ninja \\
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
  -DMOROK_BUILD_TESTS=OFF -DMOROK_BUILD_APPS=OFF \\
  -DMOROK_BUILD_FUZZERS=ON
cmake --build build-fuzz --parallel 2
./build-fuzz/morok_fuzz_utf8 -runs=2000 tests/fixtures/benchmark
./build-fuzz/morok_fuzz_rlah_decode -runs=2000 tests/fixtures/benchmark
./build-fuzz/morok_fuzz_checkpoint -runs=2000 tests/fixtures/benchmark
\`\`\`

## Critérios de qualidade

- Testes usam dados determinísticos e não dependem de rede.
- Arquivos temporários são isolados e removidos ao final.
- Testes de integração validam resultados e artefatos, não apenas códigos de saída de compilação.
- Testes em Release mantêm as verificações assertivas ativas.
- Fuzzing é opcional para builds comuns e requer Clang com libFuzzer.
- Falhas, crashes e reproduções de fuzzing devem ser convertidos em casos de regressão versionados.
- O corpus de treinamento e os conjuntos de validação/teste devem permanecer separados; fixtures sintéticas não demonstram qualidade linguística do modelo.

## Limitações explícitas

A etapa adiciona cobertura automatizada, mas não prova ausência de defeitos. O fuzzing de CI usa um orçamento limitado de execuções por commit; campanhas longas e corpus de regressão ampliado continuam apropriados para execução periódica. Os diretórios de aceitação e suporte permanecem preparados para cenários adicionais, sem serem apresentados como cobertura já existente.

## Critério de conclusão

A etapa estará validada quando CI GCC/Clang Debug/Release, ASan/UBSan e o workflow libFuzzer estiverem aprovados no commit que contém as alterações, e os testes de integração, propriedades e regressão estiverem registrados e executados pelo CTest.
