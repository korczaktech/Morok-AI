# G1

A geração G1 possui uma biblioteca executável independente, compilada como `Morok::G1`.

- `shared/include` e `shared/src`: motor, parâmetros, forward pass, BPTT, treinamento, inferência e subsistemas exclusivos da geração G1.
- `shared/tests`: testes, testes de propriedade, fuzzing e fixtures do motor G1.
- `apps`: executáveis de treinamento, inferência, reforço, avaliação e servidor.
- `scripts`: treinamento, avaliação, validação de corpus e reforço.
- `data`: dados de reforço da G1.

A G1 possui somente um modelo: **Tsukuyomi Max**, a rede neural unificada da geração. Ele utiliza o motor compartilhado da G1, possui política de geração própria e checkpoint independente.

A G1 depende apenas das bibliotecas públicas neutras da raiz e de seu motor interno. Não importa código de outras gerações.

Consulte [MODELOS.md](../MODELOS.md).

## Contrato comum de execução

O motor neural da G1 continua implementado nesta geração. A classe
`morok::model::G1ModelBackend` adapta o forward pass existente ao contrato
público `morok::model::ModelBackend`, definido em
`Morok/include/morok/model/model_backend.hpp`.

O adaptador não cria nem copia outro motor e não altera a arquitetura ou os
parâmetros da G1. Ele permite que o runtime compartilhado invoque o motor por
uma interface estável. O objeto `G1Parameters` é externo e deve permanecer
vivo e imutável durante o uso do adaptador.
