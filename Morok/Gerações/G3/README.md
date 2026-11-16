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

A Etapa 1 integrou o contrato comum e adaptou o motor G1 existente. A Etapa 2
implementou a representação byte-level UTF-8 de 261 IDs, tokens de controle,
normalização NFC configurável e validação de entrada/saída no Core compartilhado.

A Etapa 3 implementa o algoritmo REINFORCE da G3 em
`Gerações/G3/rl/`, ligado ao Core existente sem criar outro runtime. O módulo
inclui referência SFT somente para inferência, amostragem Top-K/temperatura,
baseline móvel por categoria, recompensas com pesos e penalidades explícitos,
KL por distribuição, gradientes de logits em lote, auditoria independente do
avaliador, comparação SFT/RL no mesmo conjunto fechado e estado recuperável do
treinador (baseline, RNG, configuração e proveniência).

O contrato de política recebe distribuições normalizadas de 261 IDs e delega
a aplicação do gradiente ao backend treinável que possui os pesos e o
otimizador. Como a rede GRU treinável da G3 ainda não existe, o algoritmo está
implementado e testável com políticas de contrato, mas o treinamento de pesos
reais da G3 só poderá ser executado depois da integração do backend neural.
Nenhum peso da G1 é usado como substituto.

Consulte [ARQUITETURA.md](ARQUITETURA.md) e [MODELOS.md](../MODELOS.md).
A arquitetura neural da G3 também está implementada em neural/ como
Morok::G3::Neural: embedding 261x64, GRU de duas camadas com hidden 128,
RMSNorm entre camadas, projeção 128→64, weight tying por matriz compartilhada,
inicialização determinística configurável e contagem calculada dos tensores.
O forward incremental implementa ModelBackend e mantém estados independentes
por sessão. BPTT, otimizador e treinamento SFT não fazem parte desta entrega.


A Fase 4 adiciona utilitários G3 de inferência e exportação em
`Gerações/G3/inference/`: decodificação com Top-K/temperatura, quantização
INT8 simétrica por linha, artefato `.morok` versionado com CRC32, e harness
de benchmark. Os testes usam backend determinístico para validar contratos,
formato, corrupção e isolamento concorrente. Equivalência, RAM e latência
reais permanecem pendentes até existir backend GRU G3 e pesos SFT.
