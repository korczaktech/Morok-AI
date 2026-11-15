# G1

A geração G1 possui uma biblioteca executável independente, compilada como `Morok::G1`.

- `shared/include` e `shared/src`: motor, parâmetros, forward pass, BPTT, treinamento, inferência e subsistemas exclusivos da geração G1.
- `shared/tests`: testes, testes de propriedade, fuzzing e fixtures do motor G1.
- `apps`: executáveis de treinamento, inferência, reforço, avaliação e servidor.
- `scripts`: treinamento, avaliação, validação de corpus e reforço.
- `data`: dados de reforço da G1.
- Cada perfil possui contrato, política de execução, implementação e testes próprios.

A G1 possui quatro perfis selecionáveis: `Morok::G1::Tsukuyomi`, `Morok::G1::Apollo`, `Morok::G1::Mokosh` e `Morok::G1::TsukuyomiMax`. Tsukuyomi Max é o modelo neural unificado da G1; os outros três perfis permanecem disponíveis para comparação e experimentação. Artemis não faz parte da G1.

- **Tsukuyomi:** perfil de referência, mantém o comportamento atual e opera com exatamente um agente.
- **Apollo:** perfil de velocidade, com desempenho medido em relação à média de velocidade do Tsukuyomi.
- **Mokosh:** mantém a base de execução do Tsukuyomi, com mais tempo de processamento de raciocínio e treinamento com mais épocas.
- **Tsukuyomi Max:** rede neural unificada da G1, treinada em um único ciclo com o conjunto de perguntas selecionadas e persistida em um checkpoint próprio.

Os perfis usam o motor comum da própria G1, mas não importam código privado uns dos outros.

A G1 depende apenas das bibliotecas públicas neutras da raiz e de seu motor interno. Não importa código de outras gerações.

Consulte [MODELOS.md](../MODELOS.md).
