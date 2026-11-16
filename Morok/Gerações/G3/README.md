# G3

A G3 é a geração base para a linhagem futura de modelos Morok.

## Regra arquitetural obrigatória

A G3 utiliza o motor Morok existente. Não criar outro Core, outro runtime de inferência ou uma cópia do motor em `Gerações/G3/shared/`.

A infraestrutura neutra permanece em `Morok::Core`. O contrato público de backend neural é `morok::model::ModelBackend`, em `Morok/include/morok/model/model_backend.hpp`.

## Arquitetura neural atual

O backend neural da G3 está em `neural/` como `Morok::G3::Neural`. A configuração normativa atual é a revisão **exatamente 100.000 parâmetros**:

- vocabulário: 261 IDs;
- embedding: 60 dimensões;
- GRU 1: hidden 97;
- GRU 2: hidden 68;
- duas camadas recorrentes com três portas cada;
- RMSNorm entre camadas, com escala aprendível e epsilon configurável;
- projeção 68 → 60;
- weight tying real: a cabeça de saída lê a transposta do próprio embedding;
- estado recorrente: 660 bytes por sessão FP32;
- identificador de arquitetura: `0x0302`.

A contagem é calculada a partir dos tensores efetivamente alocados. O construtor rejeita qualquer configuração que não totalize exatamente 100.000 parâmetros. A tabela de cálculo, os formatos e a regra de compatibilidade estão em [PARAMETROS-100K.md](PARAMETROS-100K.md).

A dimensão de embedding e os tamanhos ocultos anteriores (64/128) eram valores de referência da arquitetura inicial e foram substituídos nesta revisão para atingir o orçamento solicitado sem criar parâmetros artificiais. Artefatos `0x0301` são incompatíveis com a configuração atual `0x0302`.

## Perfis reservados

- **Mokosh:** eficiência e baixo consumo de memória, processamento e energia.
- **Tsukuyomi:** identidade escura/preta, com exatamente um agente.
- **Artemis:** sistema multiagente, com no mínimo três agentes.
- **Apollo:** baixa latência e rapidez de resposta, medidas por benchmark.

Os perfis compartilham o Core, o contrato de execução e a implementação-base da arquitetura G3.

## Treinamento e validação

O backend implementa o forward incremental e a interface `ModelBackend`. A arquitetura, a inicialização e o orçamento de parâmetros são testados em `neural/tests/test_gru_backend.cpp`.

BPTT, gradientes dos pesos, otimizador e treinamento SFT ainda são entregas separadas. O módulo RL continua dependendo da integração com o backend treinável real. A arquitetura de 100.000 parâmetros não significa que o modelo já esteja treinado.


## Dataset e treinamento supervisionado

O pipeline SFT está em `training/` e reutiliza diretamente o backend neural G3:

- loader JSONL com proveniência, idioma, versão, categoria, pergunta/resposta, `example_id` e `group_id`;
- validação UTF-8 e normalização NFC via ICU;
- split 85/10/5 por `group_id`, impedindo que paráfrases da mesma família atravessem splits;
- expansão combinatória limitada por eixos `expansion_axes` e variantes curadas;
- perturbação ortográfica em 45% dos exemplos de treino;
- Cross-Entropy causal com máscara de resposta, PAD ignorado e smoothing apenas em bytes comuns;
- BPTT truncado (128 passos por padrão), dropout variacional e AdamW com clipping/weight decay seletivo;
- telemetria JSONL por pergunta, exemplo e época.

Biblioteca: `Morok::G3::Training`. Executável: `morok-g3-train`. O CLI exporta os pesos FP32 finais para `.morok` usando o formato versionado 2, arquitetura `0x0302`, E=60, H1=97, H2=68 e estado de 660 bytes. Detalhes do schema e dos hiperparâmetros em [training/README.md](training/README.md).

A CI valida o pipeline com fixtures sintéticas. Nenhum corpus de produção foi adicionado nem treinado. O CLI ainda não implementa retomada de checkpoint com estado completo do AdamW e RNG; esse requisito geral de recuperabilidade permanece separado da lista funcional desta etapa.
