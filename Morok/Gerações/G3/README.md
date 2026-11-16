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
