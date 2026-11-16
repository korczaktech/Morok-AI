# G3

A G3 é a geração base para a linhagem futura de modelos Morok.

## Regra arquitetural obrigatória

A G3 utiliza o motor Morok existente. Não criar outro Core, outro runtime de
inferência ou uma cópia do motor em `Gerações/G3/shared/`.

A infraestrutura neutra permanece em `Morok::Core`. O contrato público de
backend neural é `morok::model::ModelBackend`, em
`Morok/include/morok/model/model_backend.hpp`. A G1 já possui um adaptador que
expõe seu motor existente por esse contrato.

A implementação específica da arquitetura neural G3 (GRU, RMSNorm, projeção,
weight tying e kernels) será adicionada dentro de `Gerações/G3/` e conectada ao
contrato comum. Isso é um backend arquitetural, não um segundo motor/runtime.

## Perfis reservados

- **Mokosh:** eficiência e baixo consumo de memória, processamento e energia.
- **Tsukuyomi:** identidade escura/preta, com exatamente um agente.
- **Artemis:** sistema multiagente, com no mínimo três agentes.
- **Apollo:** baixa latência e rapidez de resposta, medidas por benchmark.

Os perfis compartilham o Core, o contrato de execução e a implementação-base da
arquitetura G3. Suas políticas e configurações podem variar sem duplicar o motor.

## Estado

A Etapa 1 integra o contrato comum, adapta o motor G1 existente e estabelece
testes de compatibilidade. A arquitetura neural GRU da G3, o treinamento e a
inferência de produção permanecem para as etapas seguintes.

Consulte [ARQUITETURA.md](ARQUITETURA.md) e [MODELOS.md](../MODELOS.md).
