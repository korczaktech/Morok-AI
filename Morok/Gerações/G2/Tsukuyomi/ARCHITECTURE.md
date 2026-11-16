# Tsukuyomi G2 — arquitetura v0.1

## Identidade

- Geração: G2
- Modelo: Tsukuyomi
- Versão: 0.1.0
- Orçamento: 10.000 parâmetros por checkpoint independente; 40 checkpoints (até 400.000 parâmetros no conjunto)
- Vocabulário: 256 valores de byte
- Dimensão oculta: 32
- BPTT: até 64 transições temporais
- Implementação: C++20, treinamento do zero, sem pesos pré-treinados

## Arquitetura

Cada uma das 40 especializações do v0.1 é um modelo autoregressivo byte-level com embeddings de entrada/saída compartilhados e uma RNN de 32 dimensões. Os 10.000 parâmetros são compartilhados apenas dentro do checkpoint daquela pergunta; não são compartilhados entre as 40 perguntas. A pergunta em prompts `U:/IA:` é convertida pelos embeddings compartilhados e agregada em 13 regiões posicionais contíguas; a projeção contextual condiciona o estado oculto inicial.

Cada byte anterior da resposta é processado com seu embedding bruto e canais auxiliares de categoria linguística e fronteira contextual. A matriz recorrente compartilhada produz um candidato tanh e uma porta sigmoide de retenção. O estado combina o estado anterior e o candidato, permitindo conservar informação ao longo da resposta sem adicionar matrizes recorrentes.

A previsão é causal: ao prever o byte na posição t, o modelo só recebe bytes anteriores a t. O forward percorre o histórico disponível; durante o treinamento, a derivada atravessa até 64 transições recorrentes anteriores por BPTT truncado. A saída usa softmax estável e entropia cruzada categórica.

## Inventário de parâmetros

| Bloco | Forma | Parâmetros |
|---|---:|---:|
| Embeddings compartilhados entrada/saída | 256 × 32 | 8.192 |
| Matriz recorrente compartilhada | 32 × 32 | 1.024 |
| Projeção contextual da pergunta | 13 × 32 | 416 |
| Bias oculto | 32 | 32 |
| Bias de saída | 256 | 256 |
| Escala oculta | 32 | 32 |
| Deslocamento oculto | 32 | 32 |
| Estado inicial | 16 | 16 |
| **Total** | | **10.000** |

O código verifica os offsets e a soma por asserções estáticas. O checkpoint valida versão, contagem, checksum e valores finitos.

## Treinamento das 40 perguntas

O único corpus utilizado pelo workflow ativo é `data/feedback.txt`, com exatamente 40 pares `U:/IA:`. As 40 perguntas são alvos de treinamento em todos os ciclos. Os bytes das perguntas fornecem contexto; a máscara supervisionada seleciona os bytes das respostas IA: como alvos de perda.

Não existe conjunto de validação independente, holdout ou teste reservado no pipeline ativo. As 20 perguntas que anteriormente eram mantidas fora do treinamento foram removidas, assim como o conjunto de teste reservado de 12 perguntas. O pipeline verifica que o corpus efetivo contém exatamente 40 perguntas antes de cada ciclo.

A memória corretiva armazena gabaritos das perguntas respondidas incorretamente. Ela não acrescenta novas perguntas: ao montar o corpus do ciclo, substitui o gabarito da pergunta correspondente, preservando o total de 40 entradas. Perguntas na memória que não pertençam ao corpus são rejeitadas.

O treinador atualiza os parâmetros com gradientes da entropia cruzada, Adam, clipping global e weight decay zero. Os minibatches de alvos são embaralhados deterministicamente; os alvos dentro de cada minibatch permanecem em ordem causal. O estado do Adam é serializado separadamente e restaurado junto do checkpoint.

O LR é calculado individualmente para cada pergunta a partir do nível lexical medido contra seu gabarito. A curva fixa interpola linearmente os pontos: 0% = 1e-3; 25% = 7,5e-4; 50% = 5e-4; 75% = 2,5e-4; 85% = 1e-4; 90% ou mais = 1e-5. O LR-base técnico é 1e-3. O formulário externo de LR é ignorado e não há scheduler global por ciclo.

O pipeline mantém 40 pares independentes de estado: um checkpoint de pesos e um arquivo Adam por pergunta. Cada candidato é treinado somente com o diálogo daquela pergunta e avaliado contra sua própria resposta-alvo. A decisão de aceitação é estritamente local: se a Loss individual diminuir, somente aquele par checkpoint/Adam é atualizado; se aumentar ou empatar, somente aquele par é restaurado. Exemplo: se a pergunta 1 passa de Loss 10 para 11, a pergunta 1 é revertida, sem alterar os estados das perguntas 2 a 40. Cada pergunta conserva sua própria menor Loss histórica. Não há seleção ou rollback global de checkpoint. A exatidão lexical, F1 e similaridade são métricas de diagnóstico, não substituem a Loss.

O limite é de até 1.000 épocas por ciclo e 100 ciclos por execução. O modo contínuo é opcional e encerra quando todas as 40 respostas correspondem exatamente aos gabaritos após normalização lexical. O estado persistente guarda checkpoint, Adam, memória corretiva e histórico das mesmas 40 perguntas na branch `tsukuyomi-state`.

## Limitações

- O corpus ativo possui somente 40 perguntas; a Loss e a exatidão refletem o ajuste a esses exemplos.
- Não há avaliação de generalização independente no pipeline atual.
- Métricas lexicais não comprovam compreensão semântica.
- Não há atenção, memória externa ou uma implementação completa de GRU/LSTM.
- A agregação da pergunta em 13 regiões posicionais contíguas comprime parte da ordem interna dos bytes.
- O modelo continua byte-level, sem tokenizer de palavras.
- O treinamento é supervisionado; não implementa Reinforcement Learning nem recompensa semântica.

## Inferência

A geração é autoregressiva e greedy. A CLI roteia a pergunta para seu checkpoint individual, identificado por hash FNV-1a de 64 bits do texto UTF-8 exato. `--question-checkpoints <diretório> <pergunta> <limite>` seleciona um checkpoint; `--batch-question-checkpoints <diretório> <limite>` seleciona um checkpoint por linha de stdin. Cada checkpoint contém os mesmos 10.000 parâmetros de arquitetura, mas seus valores e momentos Adam evoluem independentemente.

## Checkpoint e persistência

O checkpoint MOROKG2T guarda os parâmetros treináveis e valida versão, tamanho e checksum. O arquivo `.adam` guarda os momentos de primeira e segunda ordem e o contador global de atualizações; ambos são necessários para retomada exata.

O workflow persiste os 40 arquivos `.ckpt` e seus 40 arquivos `.ckpt.adam`, além da memória corretiva e do histórico, na branch técnica `tsukuyomi-state`, fora da branch de código `main`. Os pares são mantidos juntos e restaurados por pergunta. As execuções são serializadas para impedir concorrência sobre os estados.
