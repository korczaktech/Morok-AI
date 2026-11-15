# G1

A geração G1 possui uma biblioteca executável independente, compilada como `Morok::G1`.

- `shared/include` e `shared/src`: motor e subsistemas exclusivos da geração G1, usados como base pelos perfis G1.
- `shared/tests`: testes e fixtures do motor compartilhado da geração.
- `apps`: executáveis de treinamento, inferência, reforço, avaliação e servidor.
- `scripts`: treinamento, avaliação, validação de corpus e reforço.
- `data`: dados de reforço da G1.
- `Mokosh/`, `Tsukuyomi/`, `Artemis/`, `Apollo/`: diretórios dos quatro perfis.

Os perfis ainda precisam de implementações e alvos próprios. O motor compartilhado da G1 não é apresentado como se já fosse quatro modelos independentes.

A G1 depende apenas das bibliotecas públicas neutras da raiz. Não importa código da G2 ou G3.

Consulte [MODELOS.md](../MODELOS.md).
