# G2

A geração G2 possui uma biblioteca executável independente, compilada como `Morok::G2`.

- `engine/include` e `engine/src`: motor por especialista, com parâmetros, estado recorrente, forward pass, BPTT, otimizador, checkpoints, treinamento e geração.
- `legacy/include` e `legacy/src`: adaptadores da G2 que agrupam os dois especialistas em um modelo G2.
- `apps`: treinamento, inferência e reforço.
- `scripts`: treinamento e reforço.
- `data`: histórico de respostas e recompensas.
- `tests` e `fixtures`: testes e corpus da G2.
- `Mokosh/`, `Tsukuyomi/`, `Artemis/`, `Apollo/`: diretórios dos quatro perfis.

O motor da G2 não inclui nem chama código privado da G1. Os perfis ainda precisam de implementações e alvos próprios; o motor G2 atual não é apresentado como quatro modelos independentes.

Consulte [MODELOS.md](../MODELOS.md).
