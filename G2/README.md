# G2

A G2 é uma geração independente. Seu código não inclui nem referencia símbolos privados da G1 ou da G3.

- `engine/include` e `engine/src`: motor recorrente de especialista, BPTT, otimizador, checkpoints, treinamento e geração. O namespace é `morok::g2::engine`.
- `model/include` e `model/src`: composição dos especialistas em um modelo G2, parâmetros agregados, inferência, treinamento e persistência. O namespace é `morok::g2::model`.
- `apps`: executáveis de treinamento, geração e reforço exclusivos da G2.
- `scripts`: fluxos de treinamento e reforço da G2.
- `data`: dados de reforço da G2.
- `tests`: testes e fixtures específicos da G2.
- `Mokosh/`, `Tsukuyomi/`, `Artemis/`, `Apollo/`: implementações e testes dos perfis da G2.

Os quatro perfis são alvos separados no CMake: `Morok::G2::Mokosh`, `Morok::G2::Tsukuyomi`, `Morok::G2::Artemis` e `Morok::G2::Apollo`. Cada perfil depende somente do motor e da composição da própria G2 e da infraestrutura pública neutra.

Consulte [MODELOS.md](../MODELOS.md).
