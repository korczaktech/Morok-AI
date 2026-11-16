# Especificação técnica completa — Tsukuyomi G2

**Projeto:** Morok AI  
**Geração:** G2 — Segunda Geração  
**Modelo:** Tsukuyomi  
**Versão pública:** 0.1.0  
**Implementação examinada:** código presente neste diretório e integração CMake/workflows da branch `main`  
**Estado deste documento:** descrição do que está implementado no código, distinguindo funcionalidades disponíveis, comportamento do pipeline e limitações.

Este documento descreve a implementação real do Tsukuyomi G2. Não é uma proposta de arquitetura futura. Quando uma capacidade aparece como não implementada, isso significa que não foi encontrada na implementação examinada.

---

## 1. Visão geral

O Tsukuyomi G2 é um modelo neural autoregressivo de linguagem, treinado do zero e que processa texto diretamente como bytes. Seu núcleo está escrito em C++20.

A implementação combina:

- vocabulário de 256 valores possíveis, correspondentes aos bytes;
- embeddings compartilhados de bytes e de características linguísticas;
- representação compacta da pergunta;
- estado oculto recorrente de 32 dimensões;
- mecanismo aprendido de retenção do estado;
- projeção de saída para os 256 valores de byte;
- treinamento supervisionado com gradientes analíticos;
- retropropagação temporal truncada (BPTT);
- otimizador Adam implementado no próprio código;
- geração autoregressiva com proteção de UTF-8;
- checkpoints de pesos e arquivos separados para o estado do otimizador;
- pipeline Python que mantém um modelo/checkpoint independente para cada uma das 100 perguntas do corpus ativo.

O modelo não utiliza pesos pré-treinados nem chama outro modelo para realizar a inferência.

### 1.1 Duas camadas que não devem ser confundidas

Há duas unidades diferentes na implementação:

1. **Modelo neural:** uma instância de Tsukuyomi possui exatamente 10.000 parâmetros treináveis.
2. **Sistema especializado de 100 perguntas:** o treinamento cíclico mantém até 100 instâncias independentes, cada uma com seu próprio checkpoint de 10.000 parâmetros e seu próprio estado Adam.

Portanto, quando os 100 checkpoints existem, o conjunto contém 100 vetores de parâmetros independentes, totalizando até **1.000.000 de valores de parâmetros** distribuídos entre os checkpoints. Isso não transforma uma instância individual em um modelo de um milhão de parâmetros.

A seleção de qual instância responde é feita pela pergunta recebida, usando um hash do texto exato em UTF-8.

## 2. Estrutura de arquivos

| Caminho | Responsabilidade |
|---|---|
| `include/morok/g2/tsukuyomi/model.hpp` | Contrato público, dimensões, configurações, estruturas e assinaturas da API C++ |
| `src/model.cpp` | Arquitetura neural, forward pass, loss, gradientes, BPTT, Adam, geração, UTF-8 e persistência binária |
| `apps/train.cpp` | Aplicação de linha de comando para treinamento, avaliação e retomada |
| `apps/generate.cpp` | Aplicação de linha de comando para geração e roteamento por pergunta |
| `scripts/treinamento_ciclico.py` | Orquestração do treinamento das 100 perguntas, aceitação/rejeição individual, memória corretiva e relatórios |
| `data/feedback.txt` | Corpus ativo de treinamento com 100 pares pergunta/resposta |
| `data/treinamento-inicial.txt` | Corpus auxiliar/histórico; não é o arquivo selecionado pelo workflow cíclico ativo |
| `data/treinamento.txt` | Corpus auxiliar/histórico; não é o arquivo selecionado pelo workflow cíclico ativo |
| `tests/test_model.cpp` | Testes C++ do modelo, gradientes, treinamento, recorrência, geração e checkpoints |
| `tests/test_treinamento_ciclico.py` | Testes Python do corpus, memória corretiva, checkpoints individuais, métricas e ciclos |

A especificação está concentrada neste documento. O antigo caminho `ARCHITECTURE.md` citado pelo README não está presente na árvore atual examinada.

## 3. Contrato público e dimensões

O cabeçalho `model.hpp` define as constantes principais:

| Constante | Valor | Significado |
|---|---:|---|
| `kVocabularySize` | 256 | Quantidade de bytes possíveis |
| `kHiddenSize` | 32 | Dimensão do estado oculto |
| `kContextSize` | 13 | Quantidade de faixas posicionais da pergunta |
| `kBpttTruncation` | 64 | Limite máximo de transições recorrentes na BPTT |
| `kParameterCount` | 10.000 | Parâmetros treináveis por checkpoint |
| `kCheckpointFormatVersion` | 8 | Versão interna do formato binário |

A versão pública do modelo é 0.1.0. A versão interna do formato de checkpoint é independente: o código atual usa formato 8 para pesos e estado Adam.

### 3.1 Estruturas públicas

- **`Model`:** contém um vetor fixo de 10.000 valores `float`.
- **`ForwardResult`:** contém 256 logits e o vetor oculto de 32 dimensões.
- **`TrainingConfig`:** configura épocas, tamanho de lote, janela BPTT, learning rate, weight decay, clipping e seed.
- **`GenerationConfig`:** configura amostragem, temperatura, top-k, top-p, penalidade de repetição, janela de repetição e seed.
- **`TrainingState`:** mantém os dois momentos do Adam, cada um com 10.000 valores `double`, e o contador de passos.
- **`TrainingReport`:** informa épocas concluídas, quantidade de bytes-alvo, loss inicial/final e BPB inicial/final.

A API também expõe inicialização, forward, cálculo de loss, treinamento, geração, cálculo de gradiente para um alvo e operações de salvar/carregar checkpoint e estado de treinamento.

## 4. Inventário exato dos 10.000 parâmetros

Os parâmetros são armazenados em um único vetor fixo. Os offsets são definidos no início de `src/model.cpp` e protegidos por verificações `static_assert`.

| Componente | Dimensão | Quantidade | Intervalo de offsets |
|---|---:|---:|---:|
| Embeddings compartilhados | 256 × 32 | 8.192 | 0–8.191 |
| Matriz recorrente oculta | 32 × 32 | 1.024 | 8.192–9.215 |
| Projeção contextual da pergunta | 13 × 32 | 416 | 9.216–9.631 |
| Bias do estado oculto | 32 | 32 | 9.632–9.663 |
| Bias de saída | 256 | 256 | 9.664–9.919 |
| Escala afim da saída | 32 | 32 | 9.920–9.951 |
| Deslocamento afim da saída | 32 | 32 | 9.952–9.983 |
| Estado inicial treinável | 16 | 16 | 9.984–9.999 |
| **Total** |  | **10.000** | **0–9.999** |

Os embeddings são compartilhados entre a representação do byte literal e os dois canais auxiliares de características linguísticas. Não existem tabelas separadas de parâmetros para cada canal.

Os `static_assert` verificam as dimensões e os limites do inventário durante a compilação. Uma mudança de dimensão exige atualizar os offsets e preservar o orçamento fechado de 10.000 parâmetros.

### 4.1 Precisão numérica

- Pesos do modelo: `float` de 32 bits.
- Momentos do Adam: `double` de 64 bits.
- O código verifica que `float` possui quatro bytes e segue o padrão IEEE 754.
- Valores não finitos são rejeitados durante validações de treinamento e carregamento.

## 5. Representação de texto em bytes

O modelo não possui tokenizer de palavras ou subpalavras. A entrada UTF-8 é convertida diretamente em bytes, e cada byte é um índice entre 0 e 255.

Isso significa que:

- uma letra ASCII ocupa um byte;
- caracteres acentuados e outros caracteres Unicode são representados por sequências de bytes UTF-8;
- o modelo aprende previsões de bytes, não IDs de tokens de palavras;
- o limite de geração é contado em bytes, não em caracteres visuais.

### 5.1 Características linguísticas auxiliares

Além do byte literal, o código calcula duas características discretas para cada byte:

**Categoria do byte:**

| Código reservado | Categoria |
|---|---|
| `0xC0` | Letra ASCII maiúscula |
| `0xC1` | Letra ASCII minúscula |
| `0xF5` | Dígito |
| `0xF6` | Espaço ou tabulação |
| `0xF7` | Quebra de linha |
| `0xF8` | Pontuação `. ? !` |
| `0xF9` | Outro byte |
| `0xFA` | Byte inicial de sequência UTF-8 multibyte |
| `0xFB` | Byte de continuação UTF-8 |

**Fronteira de palavra:**

| Código reservado | Significado |
|---|---|
| `0xFC` | Início de uma sequência classificada como palavra |
| `0xFD` | Continuação de uma sequência classificada como palavra |
| `0xFE` | Fim de sequência classificada como palavra |
| `0xFF` | Byte fora de uma sequência classificada como palavra |

A classificação de palavra considera letras ASCII, dígitos e bytes pertencentes a sequências UTF-8. O cálculo utiliza também o byte anterior.

Os códigos auxiliares são índices de embedding internos. Eles não são emitidos como texto: o softmax de saída exclui os códigos reservados `0xC0`, `0xC1` e todos os bytes a partir de `0xF5`.

Esse mecanismo fornece sinais simples de categoria e fronteira sem introduzir um tokenizer linguístico completo.

## 6. Representação da pergunta

O modelo reconhece o protocolo textual de diálogo:

`U: pergunta\nIA: resposta`

A preparação contextual procura a última linha iniciada por `U:`. O conteúdo após o marcador é tratado como pergunta, até o fim daquela linha.

A pergunta é dividida em **13 faixas posicionais contíguas**. A posição relativa de cada byte determina sua faixa. Em cada faixa, o código soma os embeddings do byte literal, da categoria e da fronteira de palavra; depois calcula a média pela quantidade de bytes naquela faixa.

O resultado é uma representação de 13 × 32 valores. Ela é projetada para o estado oculto por uma matriz treinável de 13 × 32.

Consequências dessa implementação:

- o início, o meio e o final da pergunta podem contribuir em posições diferentes;
- perguntas longas não são simplesmente reduzidas a uma soma sem posição;
- faixas sem bytes não adicionam contribuição;
- a pergunta condiciona o estado inicial da recorrência;
- a representação é específica ao protocolo `U:/IA:`; prompts livres sem esse marcador não recebem o mesmo tratamento contextual de pergunta.

## 7. Estado inicial e núcleo recorrente

O estado oculto possui 32 dimensões.

Para cada dimensão, o estado inicial combina:

1. bias oculto;
2. parâmetro inicial treinável, nas primeiras 16 dimensões;
3. soma das características posicionais da pergunta multiplicadas pela matriz de projeção contextual, quando uma pergunta válida está presente.

O resultado passa por `tanh`.

### 7.1 Transição recorrente

Para cada byte processado, a implementação soma:

- embedding do byte literal;
- embedding da categoria;
- embedding da fronteira;
- estado oculto anterior.

Essa soma é multiplicada pela matriz recorrente 32 × 32 e recebe o bias oculto. A partir do mesmo valor pré-ativação são calculados:

- candidato: `tanh(valor)`;
- retenção: função sigmoide `1 / (1 + exp(-valor))`.

O novo estado é:

`estado_novo = retencao × estado_anterior + (1 - retencao) × candidato`

A retenção é aprendida por dimensão. Ela permite que parte do estado anterior seja preservada enquanto outra parte é substituída pelo candidato.

A implementação é recorrente: a ordem dos bytes anteriores influencia o estado. Não é uma simples soma independente dos bytes.

### 7.2 Separação entre pergunta e histórico da resposta

Quando o contexto contém o protocolo `U:/IA:`:

- a pergunta é codificada na representação posicional e usada para inicializar o estado;
- a recorrência começa depois do último marcador `IA:`;
- os bytes anteriores da resposta alimentam as transições recorrentes;
- a próxima previsão depende tanto da pergunta quanto do histórico já produzido na resposta.

Isso permite que a pergunta e o histórico da resposta exerçam influências distintas.

## 8. Camada de saída e probabilidades

O estado recorrente de 32 dimensões passa por uma transformação afim treinável:

`hidden[h] = scale[h] × state[h] + shift[h]`

Para cada um dos 256 bytes, o logit é calculado pelo produto entre o embedding compartilhado daquele byte e o vetor oculto transformado, somado ao bias de saída correspondente.

O softmax:

1. ignora os códigos reservados usados pelos canais linguísticos;
2. subtrai o maior logit permitido para estabilidade numérica;
3. calcula exponenciais;
4. normaliza pela soma.

O resultado é uma distribuição de probabilidades sobre os bytes que podem ser emitidos.

## 9. Forward pass e cache incremental

A função `forward` calcula o estado e os logits para um contexto e retorna:

- logits dos 256 bytes;
- vetor oculto de 32 dimensões.

Internamente, a função `calculate` prepara a pergunta, inicializa a recorrência, processa os bytes da sequência e finaliza os logits/probabilidades.

O cache interno guarda o estado inicial, estado atual, logits, probabilidades, características contextuais e as últimas transições recorrentes necessárias à BPTT.

Existe também uma extensão incremental do cache. Se o novo contexto for uma extensão compatível do anterior, o código processa somente os bytes adicionados. Se a estrutura do contexto mudou ou a extensão não for compatível, o cache é recalculado.

Esse mecanismo é usado no treinamento para compartilhar o forward de prefixos causais dentro de um lote sem alterar os pesos durante a acumulação daquele lote.

## 10. Função de perda e objetivo supervisionado

O objetivo principal é prever o próximo byte.

Para cada posição-alvo, o modelo recebe somente o prefixo anterior e calcula a probabilidade do byte correto. A loss é a entropia cruzada negativa média:

`Loss = média(-log(probabilidade do byte-alvo))`

A probabilidade é limitada inferiormente a `1e-30` antes do logaritmo, evitando `log(0)`.

### 10.1 Máscara de alvos no formato de diálogo

A aplicação de treinamento reconhece linhas `U:` e `IA:`.

Quando o corpus está no formato de diálogo:

- os bytes da pergunta não recebem gradiente como alvos;
- os bytes do conteúdo da resposta `IA:` são alvos;
- o terminador da linha da resposta também é marcado, incluindo CR/LF conforme presente;
- cabeçalho, comentários e conteúdo fora das respostas não são alvos de treinamento.

A máscara permite treinar a previsão da resposta condicionada pela pergunta, sem otimizar a reprodução da pergunta como saída.

Se o arquivo não contém diálogos reconhecíveis, a API pode operar sem máscara e treinar todos os bytes. O workflow cíclico ativo usa o formato de diálogo e exige alvos válidos.

### 10.2 Loss e BPB

A aplicação reporta:

- loss inicial e final;
- BPB inicial e final, calculado por `Loss / ln(2)`;
- número de bytes-alvo;
- épocas concluídas.

BPB significa *bits per byte*. É uma métrica de eficiência de previsão de bytes, não uma medida direta de compreensão.

## 11. Gradientes e BPTT

Os gradientes são implementados manualmente em C++.

A derivada da saída usa a diferença entre a probabilidade prevista e o alvo one-hot. O gradiente é propagado para:

- bias de saída;
- embeddings usados na projeção de saída;
- escala e deslocamento afins;
- estado recorrente;
- matriz recorrente;
- bias oculto;
- embeddings de byte, categoria e fronteira;
- matriz de projeção contextual;
- estado inicial treinável.

### 11.1 BPTT truncada

A retropropagação temporal percorre as transições recorrentes armazenadas no cache, em ordem reversa.

A janela é limitada a 64 transições. A configuração pode escolher uma janela menor, entre 1 e 64.

Quando o histórico processado excede a janela:

- o cache mantém apenas as transições mais recentes;
- o gradiente não atravessa o limite truncado;
- o estado numérico ainda pode carregar influência de bytes anteriores, porque a recorrência forward continua preservando o estado;
- a influência antiga não recebe gradiente através das transições descartadas.

Portanto, memória no forward e alcance do gradiente são conceitos diferentes.

### 11.2 Verificação numérica

A suíte C++ compara gradientes analíticos com diferenças finitas. Também realiza uma varredura estratificada dos offsets dos 10.000 parâmetros, testa o caminho incremental e verifica se os gradientes alcançam pergunta, histórico de resposta e canais auxiliares.

## 12. Treinamento e otimizador

O treinamento é supervisionado e implementado em C++.

### 12.1 Configuração padrão do núcleo

| Configuração | Padrão |
|---|---:|
| Épocas | 1 |
| Mini-lote | 16 |
| BPTT | 64 |
| Learning rate | 0,001 |
| Weight decay | 0 |
| Gradient clipping | 1,0 |
| Seed | 42 |
| Dropout | Não utilizado |

O núcleo valida os hiperparâmetros antes de modificar o modelo. O learning rate permitido fica entre `1e-5` e `1e-2`; a janela BPTT fica entre 1 e 64; clipping deve ser positivo; valores e estados precisam ser finitos.

### 12.2 Organização dos lotes

Os bytes-alvo são ordenados causalmente. O treinamento forma grupos de até 16 alvos, respeitando também a taxa de aprendizado associada aos alvos.

Os grupos são embaralhados de forma determinística a partir da seed e do contador de passos do otimizador. Dentro de cada grupo, os alvos permanecem ordenados para que o cache incremental possa reutilizar os prefixos.

Os gradientes dos alvos do lote são acumulados e divididos pela quantidade de alvos antes da atualização. Os pesos permanecem fixos enquanto os gradientes daquele lote são acumulados.

### 12.3 Adam

O otimizador usa:

- `beta1 = 0,9`;
- `beta2 = 0,999`;
- `epsilon = 1e-8`;
- correção de viés dos dois momentos;
- momentos de primeira e segunda ordem em `double`;
- contador de passos persistido.

Antes da atualização, o código calcula a norma global do gradiente. Se ela exceder o limite, aplica clipping global proporcional.

O código possui suporte a weight decay desacoplado para embeddings e matrizes. Biases, escala, deslocamento e estado inicial não recebem esse decaimento. Porém, a aplicação CLI do Tsukuyomi G2 define explicitamente weight decay igual a zero; portanto, ele permanece desativado no fluxo ativo.

Não há dropout.

## 13. Pipeline cíclico das 100 perguntas

O script `scripts/treinamento_ciclico.py` coordena o treinamento especializado.

### 13.1 Corpus ativo

O workflow de treinamento usa:

`data/feedback.txt`

O script exige exatamente 100 pares completos `U:/IA:`. Também rejeita perguntas duplicadas e respostas-alvo duplicadas após normalização.

O arquivo `treinamento-inicial.txt` e o arquivo `treinamento.txt` não são os argumentos usados pelo workflow cíclico ativo. Eles não substituem o corpus `feedback.txt` nesse fluxo.

### 13.2 Um checkpoint por pergunta

Cada pergunta recebe um nome de checkpoint calculado com FNV-1a de 64 bits sobre os bytes UTF-8 exatos da pergunta:

`q_<hash-hexadecimal-de-16-dígitos>.ckpt`

O estado Adam correspondente usa o mesmo nome acrescido de `.adam`.

A consequência é que:

- cada pergunta possui pesos próprios;
- cada pergunta possui momentos Adam próprios;
- a atualização de uma pergunta não altera os outros 99 checkpoints;
- o roteamento exige que a pergunta seja apresentada exatamente como foi usada para gerar o hash.

Maiúsculas, minúsculas, acentos, espaços e pontuação fazem parte do texto usado no hash. Alterar a formulação pode selecionar outro checkpoint ou resultar em checkpoint inexistente.

### 13.3 Ciclo de treinamento

Em cada ciclo:

1. o script valida e carrega os 100 pares;
2. aplica as correções persistidas da memória corretiva;
3. avalia os checkpoints individuais existentes;
4. percorre as 100 perguntas;
5. calcula o nível lexical atual da pergunta;
6. define a taxa de aprendizado individual;
7. treina somente o checkpoint daquela pergunta;
8. mede a loss candidata;
9. aceita a candidata somente se a loss individual diminuir;
10. se houver regressão, restaura o checkpoint e o Adam daquela pergunta;
11. registra respostas, métricas, correções, regressões e recuperações;
12. gera o relatório consolidado.

A regra de aceitação é estritamente individual:

`Loss candidata < Loss anterior - 1e-12`

Não existe critério de aceitação por loss global. Uma piora em uma pergunta não desfaz melhorias das outras perguntas.

Quando ainda não existe checkpoint para uma pergunta, o treinamento inicializa um modelo novo com uma seed específica daquela pergunta/ciclo.

### 13.4 Taxa de aprendizado adaptativa

A taxa é calculada com base no F1 lexical atual da resposta:

| Nível lexical | Learning rate |
|---:|---:|
| 0% | 1e-3 |
| 25% | 7,5e-4 |
| 50% | 5e-4 |
| 75% | 2,5e-4 |
| 85% | 1e-4 |
| 90% ou mais | 1e-5 |

Entre os pontos, a interpolação é linear. Acima de 90%, a taxa permanece em `1e-5`.

O script cria um mapa de taxas por intervalo de bytes-alvo. A aplicação C++ associa a taxa correspondente a cada byte da resposta. O learning rate externo informado pelo workflow não substitui essa curva interna no pipeline cíclico.

Não há scheduler global de learning rate por ciclo.

### 13.5 Memória corretiva

A memória corretiva é armazenada no formato `U:/IA:`. Ela registra correções para perguntas que tiveram resposta incorreta.

Sua função é substituir o gabarito correspondente dentro do conjunto original. Ela:

- não cria perguntas novas;
- não aumenta a quantidade de perguntas;
- rejeita perguntas que não pertencem ao conjunto original;
- rejeita gabaritos conflitantes;
- valida novamente a unicidade das 100 perguntas e respostas.

Essa memória é um mecanismo de correção do treinamento supervisionado. Não é memória conversacional do modelo durante a inferência.

### 13.6 Histórico e métricas por pergunta

O histórico JSON registra, por pergunta:

- gabarito esperado;
- identificação do conjunto;
- quantidade de observações;
- quantidade de regressões;
- quantidade de recuperações;
- se já esteve correta;
- resposta mais recente;
- último F1 lexical;
- última loss e BPB;
- melhor loss observada;
- ciclo e execução mais recentes;
- observações anteriores, limitadas às 200 mais recentes por pergunta.

Também existe um contador global de ciclos.

### 13.7 Critério de parada

O script permite até 100 ciclos por execução e até 1.000 épocas por pergunta em cada ciclo, respeitando os limites passados pelo workflow.

O modo contínuo é opcional. Quando ativado, o pipeline encerra ao atingir correspondência lexical exata nas 100 respostas. Se o objetivo não for atingido dentro do limite de ciclos, a execução termina com erro.

A configuração atual do workflow mantém o modo contínuo desativado por padrão.

## 14. Avaliação implementada

A avaliação ocorre individualmente para cada pergunta, usando seu checkpoint correspondente.

As métricas registradas incluem:

- **Correspondência exata:** após normalização lexical;
- **F1 lexical:** sobre frequências de palavras normalizadas;
- **Similaridade de caracteres:** razão de similaridade de sequência;
- **Loss:** entropia cruzada dos bytes-alvo;
- **BPB:** loss convertida em bits por byte;
- **Bytes-alvo:** quantidade de bytes da resposta usada na avaliação;
- **Repetição consecutiva:** tamanho da maior sequência de caracteres iguais dividido pelo tamanho da resposta;
- **Diversidade de caracteres:** quantidade de caracteres distintos dividida pelo tamanho da resposta;
- **Learning rate individual:** taxa calculada para o nível lexical atual.

A normalização lexical usa casefold, decomposição Unicode NFKD, remoção de marcas combinantes e extração de sequências alfanuméricas. O F1 é calculado por sobreposição de frequências.

A classificação de habilidade do relatório agrupa perguntas em categorias como conceito, fato, composição e conversa.

**Limitação metodológica importante:** as 100 perguntas participam diretamente do treinamento. Não há holdout, conjunto de validação independente ou conjunto de teste reservado no pipeline cíclico ativo. A correspondência exata mede ajuste às perguntas treinadas, não generalização para perguntas inéditas nem compreensão semântica.

## 15. Geração autoregressiva

A geração recebe um prompt em bytes, prepara a pergunta quando encontra o protocolo `U:`, processa o histórico após o último marcador `IA:` e prevê um byte por iteração.

A cada passo:

1. calcula os logits a partir do estado oculto;
2. determina quais bytes são válidos para a posição UTF-8 atual;
3. aplica, se configurada, penalidade de repetição;
4. seleciona o byte por greedy ou amostragem;
5. valida o byte UTF-8;
6. acrescenta o byte à saída;
7. atualiza o estado recorrente para a próxima previsão.

### 15.1 Decodificação greedy

É o modo padrão. Escolhe o byte permitido com maior logit. Em caso de empate, escolhe o menor valor de byte, tornando o desempate determinístico.

### 15.2 Amostragem configurável

A API e a CLI suportam:

- `--sample`: ativa amostragem;
- `--temperature`: ajusta a escala dos logits;
- `--top-k`: limita os candidatos aos K maiores scores;
- `--top-p`: mantém o conjunto mínimo de candidatos que alcança a massa acumulada indicada;
- `--repetition-penalty`: penaliza bytes já vistos na janela;
- `--repetition-window`: define o tamanho da janela de repetição;
- `--seed`: controla o gerador pseudoaleatório.

Os valores são validados. A temperatura precisa estar entre `1e-6` e 100; top-p entre zero exclusivo e 1; penalidade de repetição deve ser maior ou igual a 1.

A penalidade é aplicada aos logits dos bytes repetidos na janela, sem alterar os parâmetros do modelo.

### 15.3 Proteção UTF-8

O decodificador mantém o estado da sequência UTF-8:

- quantidade de bytes de continuação esperados;
- limite mínimo e máximo permitido para o próximo byte.

São rejeitados bytes iniciais inválidos, continuações fora do intervalo e sequências que produziriam codificações inválidas, incluindo overlongs, surrogate halves e valores acima de U+10FFFF.

O decodificador também evita iniciar uma sequência multibyte quando não há bytes restantes suficientes no limite de geração.

### 15.4 Encerramento

A geração termina quando:

- atinge o limite máximo de bytes;
- não há candidatos válidos;
- encontra quebra de linha (`\n` ou `\r`);
- encontra byte nulo (`\0`).

A aplicação CLI limita a geração entre 1 e 4.096 bytes. O pipeline de avaliação das 100 perguntas usa limite de 160 bytes por resposta.

Não existe um token EOS aprendido separado; o encerramento do protocolo é feito pelos terminadores descritos acima.

## 16. Aplicações de linha de comando

### 16.1 Treinador

Executável: `morok-g2-tsukuyomi-train`.

Responsabilidades:

- ler o corpus como bytes;
- validar tamanho entre 2 bytes e 512 MiB;
- reconhecer o formato de diálogo e criar a máscara de respostas;
- inicializar o modelo ou carregar pesos e Adam com `--resume`;
- carregar mapa opcional de learning rate por intervalo;
- executar o treinamento;
- gravar checkpoint e estado Adam;
- exibir métricas do treinamento.

Também oferece:

- `--evaluate <corpus> <checkpoint>`: avaliação sem atualização dos pesos;
- `--evaluate-each <corpus> <checkpoint>`: loss, BPB e quantidade de alvos por diálogo.

A retomada exige que checkpoint e arquivo Adam estejam presentes e válidos em conjunto. Se um estiver ausente ou corrompido, a aplicação recusa a retomada.

### 16.2 Gerador

Executável: `morok-g2-tsukuyomi-generate`.

Modos disponíveis:

- checkpoint único com prompt;
- `--batch`: várias perguntas usando o mesmo checkpoint;
- `--question-checkpoints`: seleciona o checkpoint individual para uma pergunta;
- `--batch-question-checkpoints`: recebe uma pergunta por linha e seleciona um checkpoint para cada uma.

O modo individual calcula o hash da pergunta exata e carrega o arquivo correspondente. O modo batch roteado faz o mesmo para cada linha recebida pela entrada padrão.

A CLI não executa comandos do sistema operacional, não acessa arquivos arbitrários e não realiza buscas na internet. Sua saída é texto gerado pelo modelo.

## 17. Checkpoints e persistência

### 17.1 Checkpoint dos pesos

Formato binário atual:

- magic: `MOROKG2T`;
- versão interna: 8;
- quantidade de parâmetros: 10.000;
- checksum FNV-1a de 64 bits dos bytes dos pesos;
- vetor de 10.000 valores `float`.

Ao carregar, o código verifica magic, versão, contagem, tamanho exato, ausência de bytes extras, valores finitos e checksum. O modelo só é substituído pelo candidato carregado depois que todas as verificações passam.

### 17.2 Estado Adam

Formato binário atual:

- magic: `MOROKADM`;
- versão interna: 8;
- quantidade de parâmetros: 10.000;
- contador de passos;
- checksum;
- 10.000 momentos de primeira ordem `double`;
- 10.000 momentos de segunda ordem `double`.

O carregamento verifica formato, tamanho, ausência de bytes extras, valores finitos, não negatividade dos segundos momentos e checksum.

### 17.3 Pareamento obrigatório

O arquivo `.ckpt` e seu arquivo `.ckpt.adam` formam uma unidade lógica. Restaurar somente os pesos e descartar os momentos altera o estado do otimizador e não representa uma retomada fiel.

O pipeline verifica arquivos ausentes, pares incompletos e arquivos vazios. Quando uma candidata é rejeitada, checkpoint e Adam são restaurados juntos.

Os salvamentos C++ atuais escrevem diretamente no caminho de destino. A gravação da memória corretiva e do histórico JSON pelo script Python usa arquivo temporário seguido de substituição.

## 18. Persistência externa do treinamento

O workflow `.github/workflows/treinamento-g2.yml` usa a branch técnica `tsukuyomi-state` para persistir o estado de treinamento entre execuções.

O fluxo implementado:

1. recupera checkpoints e estados Adam individuais;
2. recupera a memória corretiva;
3. recupera o histórico das perguntas, quando presente;
4. executa o pipeline;
5. salva os 100 pares checkpoint/Adam;
6. salva a memória corretiva;
7. salva o histórico;
8. publica artefatos de relatório e checkpoints da execução.

O estado persistente é separado do código-fonte em `main`.

**Estado conhecido após o reset solicitado anteriormente:** os checkpoints e arquivos Adam foram apagados da branch `tsukuyomi-state`, assim como o histórico de ciclos/perguntas. A memória corretiva foi preservada. Portanto, o código e o pipeline de treinamento continuam implementados, mas não havia pesos treinados persistidos para retomada no estado verificado após aquele reset. Uma execução futura sem checkpoints começará a inicializar novamente os modelos individuais.

## 19. Integração CMake e workflows

### 19.1 CMake

O projeto compila o Tsukuyomi como biblioteca estática independente:

- biblioteca: `morok_g2_tsukuyomi`;
- alias CMake: `Morok::G2::Tsukuyomi`;
- aplicação de treinamento: `morok_g2_tsukuyomi_train`;
- aplicação de geração: `morok_g2_tsukuyomi_generate`;
- teste C++: `morok_test_g2_tsukuyomi`.

A biblioteca G2 compila `Gerações/G2/Tsukuyomi/src/model.cpp` e usa o cabeçalho próprio da geração. O CMake comenta explicitamente que a implementação G2 é isolada das fontes de implementação da G1.

O projeto utiliza C++20. Os testes e aplicações são controlados pelas opções CMake `MOROK_BUILD_TESTS` e `MOROK_BUILD_APPS`.

### 19.2 Workflow de CI — `g2.yml`

O workflow de CI é acionado por alterações nos caminhos do Tsukuyomi, testes, CMake e pelo acionamento manual. Ele:

- valida o corpus e os testes Python;
- instala Ninja e SQLite;
- configura CMake em Release;
- compila biblioteca, treinador, gerador e teste;
- executa CTest para o modelo G2 e isolamento entre gerações;
- realiza smoke tests de treinamento e inferência;
- testa geração greedy e amostragem;
- testa parsing de BOM, espaços e CRLF;
- testa métricas individuais das 100 perguntas;
- testa roteamento por checkpoint individual.

A existência do workflow descreve validação automatizada configurada. O resultado de uma execução específica deve ser consultado no GitHub Actions; este documento não declara que toda execução histórica ou futura passou.

### 19.3 Workflow de treinamento — `treinamento-g2.yml`

O workflow de treinamento é separado do CI. O job pode ser acionado por push com a marca `[train-g2]` na mensagem do commit ou por execução manual com Tsukuyomi selecionado.

Os limites validados pelo workflow são:

- 1 a 1.000 épocas por ciclo;
- 1 a 100 ciclos;
- treinamento contínuo opcional.

O padrão atual do workflow é 50 épocas por pergunta/ciclo, até 100 ciclos e treinamento contínuo desativado. O job usa Ubuntu 24.04 e possui timeout de seis horas.

O workflow também serializa execuções que compartilham o estado persistente, recupera/salva a branch técnica e publica relatório e artefatos. Este documento não executa treinamento.

## 20. Testes implementados

### 20.1 Testes C++

A suíte `test_model.cpp` cobre, entre outros pontos:

- gradiente analítico comparado com diferenças finitas;
- amostragem estratificada dos 10.000 offsets;
- gradientes da representação da pergunta;
- gradientes dos embeddings e dos canais linguísticos;
- faixas posicionais contíguas;
- distinção entre perguntas que diferem no início;
- influência do histórico anterior da resposta;
- influência recorrente depois de mais de uma janela BPTT;
- ablações da projeção contextual e da matriz recorrente;
- sensibilidade à ordem dos bytes;
- influência da pergunta no gradiente da resposta;
- encerramento greedy no terminador;
- penalidade de repetição e opções de decodificação;
- rejeição de hiperparâmetros inválidos sem alteração dos pesos;
- escopo do weight decay;
- momentos Adam, correção de viés e clipping global;
- equivalência de retomada em blocos e treinamento contínuo;
- diferença entre janelas BPTT;
- propagação de gradiente até a pergunta dentro da janela;
- corte do gradiente no limite truncado;
- mapa de learning rate por resposta;
- treinamento somente nos bytes marcados como resposta;
- gravação e leitura de checkpoint e estado Adam.

As verificações de assert permanecem ativas também em builds Release.

### 20.2 Testes Python

A suíte `test_treinamento_ciclico.py` cobre:

- existência de exatamente 100 perguntas no fluxo;
- rejeição de perguntas/respostas duplicadas;
- curva de learning rate adaptativa por pergunta;
- ausência de scheduler global por ciclo;
- preservação do orçamento de treinamento;
- substituição de gabaritos sem adicionar perguntas;
- rejeição de perguntas fora do conjunto;
- rejeição de respostas duplicadas após correções;
- geração correta dos intervalos de learning rate;
- persistência e leitura da memória corretiva;
- registro de erros sem duplicar perguntas;
- persistência do contador global;
- checkpoint e Adam independentes por pergunta;
- aceitação/rejeição de loss independente;
- hash do checkpoint;
- restauração conjunta de pesos e Adam;
- recusa de estado incompleto;
- parada do modo contínuo somente quando as 100 perguntas estão corretas;
- métricas finitas;
- histórico individual e melhor loss por pergunta;
- relatório individual e normalização lexical.

## 21. O que está implementado e o que não está

### 21.1 Implementado no código examinado

- Modelo neural byte-level de 10.000 parâmetros por checkpoint.
- Embeddings compartilhados e canais linguísticos auxiliares.
- Representação posicional da pergunta com 13 faixas.
- Estado oculto recorrente de 32 dimensões.
- Retenção aprendida por dimensão.
- Projeção de saída para bytes.
- Softmax estável.
- Loss de previsão do próximo byte.
- Máscara de treinamento para respostas `IA:`.
- Gradientes analíticos manuais.
- BPTT truncada até 64 transições.
- Adam, correção de viés e clipping global.
- Checkpoint dos pesos e persistência dos momentos Adam.
- Geração greedy e amostragem configurável.
- Proteção de UTF-8 durante geração.
- Roteamento para até 100 checkpoints especializados por pergunta.
- Treinamento cíclico independente por pergunta.
- Aceitação/rejeição por loss individual.
- Learning rate adaptativo por F1 lexical.
- Memória corretiva supervisionada.
- Histórico e métricas individuais.
- CLI de treinamento, avaliação e geração.
- Integração CMake, testes e workflows.

### 21.2 Não implementado no Tsukuyomi G2

Não foi encontrada implementação destas capacidades no código específico do Tsukuyomi G2:

- mecanismo de atenção;
- arquitetura Transformer;
- tokenizer de palavras ou subpalavras;
- BPE, SentencePiece ou vocabulário semântico;
- memória externa consultável durante a inferência;
- memória conversacional persistente entre sessões;
- recuperação semântica de documentos;
- embeddings vetoriais externos;
- busca na internet;
- acesso livre a arquivos;
- execução de comandos do sistema operacional;
- chamada direta de ferramentas ou APIs;
- interpretação de instruções como um agente autônomo;
- subsistemas cognitivos completos integrados ao modelo;
- treinamento online durante uma conversa;
- avaliação de generalização em conjunto independente no pipeline ativo.

A memória corretiva do pipeline não equivale a memória conversacional. O histórico de treinamento também não é apresentado ao modelo como contexto automático durante a inferência.

A arquitetura recorrente presente no código não deve ser descrita como Transformer ou como mecanismo de atenção.

## 22. Limitações e interpretação dos resultados

1. **Especialização restrita:** o pipeline ativo treina uma instância por pergunta do conjunto fixo. Uma pergunta inédita não possui necessariamente checkpoint roteável.
2. **Dependência de texto exato:** o roteamento usa hash da pergunta UTF-8 exata. Variações de formulação podem não encontrar o mesmo checkpoint.
3. **Corpus pequeno e fechado:** o fluxo ativo usa 100 pares e não possui holdout independente.
4. **Ajuste lexical não comprova semântica:** F1 e correspondência exata medem semelhança textual com os gabaritos, não entendimento.
5. **Contexto especializado:** o caminho de pergunta usa o marcador `U:` e 13 faixas posicionais; não é um sistema geral de compreensão de contexto.
6. **Janela de gradiente limitada:** o estado recorrente pode carregar informação antiga, mas o gradiente é truncado em até 64 transições.
7. **Saída em bytes:** limites e decodificação são baseados em bytes UTF-8, não em tokens ou palavras.
8. **Sem memória de conversa:** não existe armazenamento automático de mensagens anteriores entre chamadas do gerador.
9. **Sem execução de comandos no modelo:** a saída é texto; a CLI não transforma a resposta em uma ação do sistema operacional.
10. **Estado treinado não acompanha o código:** checkpoints e momentos Adam ficam na branch técnica, separados da implementação. Após o reset registrado na seção 18, o estado treinado anterior não está disponível para retomada.

## 23. Compilação e teste

A partir do diretório `Morok/`, a configuração usada pelo workflow é:

```sh
cmake -S . -B build/g2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMOROK_BUILD_TESTS=ON \
  -DMOROK_BUILD_APPS=ON

cmake --build build/g2 --parallel 2 \
  --target morok_g2_tsukuyomi \
           morok_g2_tsukuyomi_train \
           morok_g2_tsukuyomi_generate \
           morok_test_g2_tsukuyomi

ctest --test-dir build/g2 \
  -R "g2.tsukuyomi.model|architecture.generation_isolation" \
  --output-on-failure

python3 -m unittest discover \
  -s "Gerações/G2/Tsukuyomi/tests" \
  -p "test_*.py" -v
```

Esses comandos compilam e testam a implementação. Eles não iniciam o treinamento cíclico das 100 perguntas.

## 24. Conclusão técnica

O Tsukuyomi G2 0.1.0 possui um núcleo neural byte-level recorrente, com 10.000 parâmetros por instância, treinamento supervisionado, BPTT truncada, Adam, geração UTF-8 segura e persistência verificável.

O sistema de treinamento acrescenta uma camada de especialização com até 100 checkpoints independentes, um para cada pergunta do corpus ativo. Essa camada inclui controle de regressão por loss individual, learning rate adaptativo, memória corretiva e histórico auditável.

A implementação é um modelo experimental especializado. Não é, nesta versão, um agente geral com atenção, memória conversacional, pesquisa externa ou execução de ferramentas. O comportamento observado deve ser avaliado dentro do conjunto de treinamento e das limitações descritas acima.
