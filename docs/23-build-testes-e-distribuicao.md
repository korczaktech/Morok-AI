# 23. Build, testes, distribuição e observabilidade

## 23.1 Plataforma de referência

- Linguagem: C++20.
- Build: CMake 3.25 ou superior.
- Plataforma inicial: Linux x86-64.
- Compiladores de referência: GCC 13+ e Clang 17+.
- Testes integrados ao CTest.
- Dependências mínimas: SQLite3 para IA-MEM; núcleo numérico da G1 implementado em C++ sem framework pesado de machine learning.
- BLAS/GPU não são dependências da G1; só serão avaliadas se benchmarks demonstrarem benefício.

A compatibilidade com Android, Windows e macOS será uma etapa posterior, após estabilizar Linux.

## 23.2 Estrutura de diretórios proposta

```
Morok-AI/
  CMakeLists.txt
  CMakePresets.json
  README.md
  docs/
  include/morok/
    core/
    lang/
    model/
    learn/
    memory/
    reason/
    tools/
    comm/
  src/
    core/
    lang/
    model/
    learn/
    memory/
    reason/
    tools/
    comm/
  tests/
    unit/
    integration/
    acceptance/
  benchmarks/
  configs/
  examples/
  scripts/
  data/
    README.md
  artifacts/
    README.md
```

Corpora, checkpoints privados e dados pessoais não entram no Git por padrão. O repositório mantém apenas documentação, código, configurações de exemplo e dados pequenos de teste com licença apropriada.

## 23.3 Perfis de build

- Debug: símbolos, verificações e sanitizers quando suportados.
- Release: otimização -O2, sem remover verificações de entrada e limites.
- Reproducible: semente e configuração fixas, versões registradas.

Não usar -Ofast inicialmente, pois pode alterar semântica de operações de ponto flutuante e dificultar validação numérica.

## 23.4 Estratégia de testes

- Unitários: funções matemáticas, UTF-8, hashing, serialização, contagem de parâmetros.
- Integração: modelo + treinador, IA-LANG + modelo, IA-MEM + busca.
- Aceitação: testes do documento 18.
- Fuzzing: parser de checkpoint, UTF-8 e estruturas de agrupamento.
- Sanitizers: AddressSanitizer e UndefinedBehaviorSanitizer em Debug quando disponíveis.
- Regressão: fixtures determinísticas e logits de referência.

## 23.5 CI

Cada alteração deve executar:
1. formatação/verificação estática;
2. build Debug;
3. testes unitários;
4. build Release;
5. testes de integração;
6. verificação de inventário;
7. testes de serialização/checkpoint.

Benchmarks não bloqueiam cada commit inicialmente, mas serão executados em releases e alterações do núcleo. Não aceitar teste que apenas compile sem validar resultado.

## 23.6 Observabilidade

Cada experimento registra:
- commit e versão de arquitetura;
- compilador, versão e flags;
- CPU, RAM e sistema;
- semente;
- corpus e hash;
- configuração completa;
- parâmetros e checkpoint;
- perda, BPB, gradientes, normas de estado;
- latência, throughput, pico de RAM e tempo de treinamento;
- falhas e motivo de parada.

Métricas devem ter unidade, janela e denominador explícitos.

## 23.7 Distribuição

Artefatos incluem versão, arquitetura, inventário, checksum e metadados. Não carregar checkpoint incompatível. O runtime deve permitir executar somente inferência sem incluir ferramentas de treinamento quando isso reduzir superfície e tamanho.

## 23.8 Critério de release

Um release G1 exige build reproduzível, testes de aceitação aprovados, checkpoint de demonstração treinado do zero, relatório de benchmark e limitações publicadas. Sem esses itens, classificar como protótipo experimental.
