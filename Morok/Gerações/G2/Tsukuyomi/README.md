# Tsukuyomi — G2

**Geração:** Segunda Geração (G2)  
**Modelo:** Tsukuyomi  
**Versão:** 0.1.0  
**Orçamento:** exatamente 10.000 parâmetros treináveis

O Tsukuyomi é um modelo neural autoregressivo byte-level, treinado do zero. Usa embeddings compartilhados, canais linguísticos auxiliares, uma célula recorrente com retenção aprendida, estado oculto de 32 dimensões e BPTT truncado em até 64 etapas. Não utiliza pesos pré-treinados nem dependências de modelos externos.

## Estrutura

- `include/morok/g2/tsukuyomi/model.hpp`: contrato público.
- `src/model.cpp`: parâmetros, forward, gradientes, Adam, geração e checkpoints.
- `apps/train.cpp`: treinamento e retomada de checkpoints.
- `apps/generate.cpp`: inferência.
- `scripts/treinamento_ciclico.py`: treinamento cíclico das 40 perguntas.
- `tests/`: testes do modelo e do pipeline.
- `ARCHITECTURE.md`: especificação técnica.

## Compilação

A partir de `Morok/`:

```sh
cmake -S . -B build/g2 -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON -DMOROK_BUILD_APPS=ON
cmake --build build/g2 --target morok_g2_tsukuyomi morok_g2_tsukuyomi_train morok_g2_tsukuyomi_generate morok_test_g2_tsukuyomi --parallel 2
ctest --test-dir build/g2 -R g2.tsukuyomi.model --output-on-failure
```

## Treinamento

O workflow **Treinamento — G2** utiliza exclusivamente `data/feedback.txt`, que contém exatamente 40 pares `U:/IA:`. Todas as 40 perguntas participam diretamente do treinamento em cada ciclo. Não existe conjunto de validação, holdout ou teste reservado no pipeline ativo. As 20 perguntas que eram usadas apenas para validação foram removidas do repositório e do workflow.

O padrão executa até 1.000 épocas por ciclo e até 100 ciclos por execução. O treinamento contínuo é opcional e, quando ativado, encerra quando as 40 respostas atingem correspondência lexical exata com os gabaritos.

A seleção do checkpoint usa a própria Loss de treinamento: um candidato é aceito quando a Loss final do ciclo é menor que a Loss inicial. Caso contrário, os pesos e o estado Adam anteriores são restaurados juntos. Isso evita que um ciclo que piorou o ajuste substitua o melhor checkpoint disponível. A Loss é calculada sobre os bytes das respostas IA:, enquanto as perguntas são fornecidas como contexto.

A memória corretiva não cria perguntas adicionais. Ela atualiza o gabarito correspondente dentro das mesmas 40 entradas. O script verifica que o corpus combinado continua contendo exatamente 40 perguntas.

O learning rate é individual por pergunta e depende do nível lexical atual. A curva fixa usa os pontos 0% = 1e-3, 25% = 7,5e-4, 50% = 5e-4, 75% = 2,5e-4, 85% = 1e-4 e 90% ou mais = 1e-5, com interpolação linear. O formulário de LR externo é ignorado. Não há scheduler global por ciclo e o weight decay permanece zero.

Pesos, momentos Adam, memória corretiva e histórico das 40 perguntas são persistidos na branch técnica `tsukuyomi-state`. O histórico registra as respostas, acertos, regressões e recuperações ao longo dos ciclos e execuções. Isso é treinamento supervisionado com replay corretivo, não Reinforcement Learning.

## Inferência

A geração é autoregressiva e greedy. As 13 regiões posicionais da pergunta inicializam o estado recorrente; cada byte gerado atualiza esse estado. A CLI batch carrega o checkpoint uma vez e atende várias perguntas recebidas por stdin.

```sh
build/g2/morok-g2-tsukuyomi-generate tsukuyomi-g2-v0.1.ckpt "Olá" 120
```

## Limitações

A versão 0.1 possui 10.000 parâmetros e um corpus pequeno de 40 perguntas. Acertar essas perguntas mede o ajuste aos exemplos treinados, não comprova compreensão semântica nem generalização para perguntas inéditas. O modelo ainda não possui atenção, memória externa ou subsistemas cognitivos completos.
