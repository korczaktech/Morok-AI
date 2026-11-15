# G1

A geração G1 possui uma biblioteca executável independente, compilada como `Morok::G1`.

- `shared/include` e `shared/src`: motor, parâmetros, forward pass, BPTT, treinamento, inferência e subsistemas exclusivos da geração G1.
- `shared/tests`: testes, testes de propriedade, fuzzing e fixtures do motor G1.
- `apps`: executáveis de treinamento, inferência, reforço, avaliação e servidor.
- `scripts`: treinamento, avaliação, validação de corpus e reforço.
- `data`: dados de reforço da G1.
- Cada diretório de perfil contém seu próprio contrato, política de execução, implementação e teste.

Os alvos `Morok::G1::Mokosh`, `Morok::G1::Tsukuyomi`, `Morok::G1::Artemis` e `Morok::G1::Apollo` são compilados separadamente. Os perfis usam o motor comum da própria G1, mas não importam código privado uns dos outros.

A G1 depende apenas das bibliotecas públicas neutras da raiz e de seu motor interno. Não importa código da G2 ou G3.

Consulte [MODELOS.md](../MODELOS.md).
