# Tsukuyomi G2 — arquitetura v0.1

## Identidade

- Geração: G2
- Modelo: Tsukuyomi
- Versão: 0.1.0
- Perfil: maior capacidade da G2
- Orçamento exato: 10.000 parâmetros treináveis
- Vocabulário: 256 valores de byte
- Dimensão oculta: 32
- Estado recorrente: vetor oculto de 32 dimensões com retenção temporal aprendida
- Célula temporal: candidato tanh e porta sigmoide de retenção, calculados com a matriz recorrente compartilhada
- BPTT: retropropagação truncada em até 64 transições temporais
- Implementação: C++20, sem pesos pré-treinados e sem dependência de modelos externos

## Arquitetura

O v0.1 é um modelo neural autoregressivo de bytes com embeddings de entrada/saída compartilhados, enriquecidos por dois canais linguísticos aprendidos e uma RNN de 32 dimensões. A pergunta em prompts U:/IA: é convertida pelos embeddings compartilhados e agregada em 13 regiões posicionais contíguas, calculadas proporcionalmente ao comprimento da pergunta; a projeção contextual condiciona o estado oculto inicial. Cada byte anterior da resposta é processado com seu embedding bruto, um protótipo de categoria linguística e um protótipo de fronteira contextual. Esses três vetores são somados ao estado anterior e projetados pela matriz 32×32 compartilhada, seguida de bias. A pré-ativação compartilhada produz um candidato tanh e uma porta sigmoide de retenção. A pré-ativação é `z_t = W(e_byte + e_categoria + e_fronteira + h_(t-1)) + b`. O novo estado combina o estado anterior e o candidato: `h_t = r_t * h_(t-1) + (1-r_t) * tanh(z_t)`, com `r_t = sigmoid(z_t)`. Assim, a rede aprende uma taxa de atualização/esquecimento por dimensão sem adicionar matrizes ou parâmetros. A mesma matriz é reutilizada em todas as etapas temporais, evitando adicionar parâmetros. Essa célula é uma recorrência com retenção aprendida, não uma implementação GRU/LSTM. O estado final passa por escala e deslocamento aprendidos; a cabeça de saída calcula 256 logits usando a matriz de embeddings compartilhada.

A previsão é causal: ao prever o byte na posição t, o modelo só recebe bytes anteriores a t. O estado oculto transporta informação temporal entre bytes e a porta de retenção permite conservar componentes úteis por mais de 64 bytes. Durante o treinamento, a derivada da perda atravessa até 64 transições recorrentes anteriores por Truncated Backpropagation Through Time (TBPTT). Quando a sequência é maior que a janela, o forward processa o histórico inteiro e a memória pode persistir, mas o gradiente é interrompido no limite configurado; portanto, o modelo não recebe crédito direto de uma perda atual para transições anteriores a essa janela. A distribuição de saída usa softmax estável e a função objetivo é entropia cruzada categórica.

Os canais auxiliares reutilizam 13 linhas da própria tabela de embeddings (bytes `0xC0`, `0xC1` e `0xF5`–`0xFF`), sem acrescentar parâmetros. Esses valores são inválidos como bytes iniciais UTF-8 e, por isso, não colidem com bytes de texto UTF-8 válido. Eles também são mascarados na softmax de treinamento e bloqueados pelo decodificador UTF-8, evitando que os vetores de característica concorram como classes de saída. As categorias distinguem maiúsculas, minúsculas, dígitos, espaços, quebras de linha, pontuação de sentença, outros símbolos/pontuação e bytes UTF-8 de início e continuação. O segundo canal codifica início, continuação, fim ou transição neutra de palavra. As mesmas características são agregadas à representação posicional da pergunta. Os bytes continuam sendo a representação primária e a saída continua no vocabulário integral de 256 bytes; não há tokenizer nem vocabulário de palavras separado. Os protótipos são compartilhados com linhas reservadas a bytes inválidos UTF-8, mantendo o orçamento fechado sem interferir com caracteres válidos.

## Inventário auditável

| Bloco | Forma | Parâmetros |
|---|---:|---:|
| Embeddings compartilhados entrada/saída | 256 × 32 | 8.192 |
| Matriz de transição recorrente compartilhada | 32 × 32 | 1.024 |
| Projeção da pergunta para o estado inicial | 13 × 32 | 416 |
| Bias oculto | 32 | 32 |
| Bias de saída | 256 | 256 |
| Escala oculta | 32 | 32 |
| Deslocamento oculto | 32 | 32 |
| Estado inicial | 16 | 16 |
| **Total** | | **10.000** |

Os offsets e a soma são verificados por asserção estática no código. O checkpoint rejeita versões, contagens, checksums e valores não finitos incompatíveis. A versão 8 do formato interno impede carregar pesos treinados com a agregação posicional cíclica anterior; a identidade pública do Tsukuyomi continua 0.1.

## Treinamento

O treinador percorre os bytes-alvo e atualiza os parâmetros com gradientes da entropia cruzada, TBPTT de até 64 etapas, Adam, clipping global e weight decay. Os minibatches de alvos são embaralhados deterministicamente a cada época; dentro de cada minibatch, os alvos permanecem em ordem causal e compartilham o estado recorrente incremental enquanto os pesos ficam fixos. O texto e os contextos causais nunca são embaralhados. A semente depende do contador Adam, preservando a sequência de atualizações ao retomar checkpoints. Em corpora com pares U:/IA:, o executável cria uma máscara que seleciona somente os bytes posteriores ao prefixo IA: como alvos de treinamento; os bytes da pergunta continuam disponíveis como contexto. Em arquivos sem pares de diálogo reconhecidos, todos os bytes continuam sendo alvos. Os gradientes são acumulados em mini-lotes (16 alvos por padrão) e aplicados uma vez por lote. O estado do Adam (momentos de primeira e segunda ordem e contador global de atualizações) é serializado separadamente e restaurado na retomada. A versão 8 desse arquivo invalida momentos calculados com a representação contextual posicional anterior. O learning rate inicial padrão é 0,001 (0,1%), aceitando valores iniciais entre 1e-7 e 0,1. Após cada ciclo, independentemente do resultado da validação, o executor reduz a taxa em 10% (multiplica por 0,9), respeitando o piso absoluto de 1e-7. A taxa reduzida é usada no ciclo seguinte; a queda não depende de platô nem de melhora na validação. O ranking é lexicográfico: primeiro o número de respostas exatas; F1 lexical e similaridade média só desempatem quando a exatidão empata, com melhora mínima de 0,005; Adam usa β1=0,9, β2=0,999 e epsilon=1e-8. O clipping limita a norma global a 1,0. O weight decay seletivo foi elevado para 0,01 após a primeira execução de 200 épocas mostrar forte queda da loss de treino sem melhora de validação; o objetivo é reduzir sobreajuste. O weight decay padrão é 0,01 e atua somente em embeddings e matrizes, não em biases, escala, deslocamento ou estado inicial. O relatório expõe loss e train_bpb calculados sobre os alvos selecionados, por ciclo. Após cada validação, pesos e momentos Adam são copiados juntos quando a pontuação melhora; ao final, o pipeline restaura o melhor par checkpoint/Adam antes de consultar o teste reservado. Assim, uma última época pior não substitui o melhor modelo observado. O pipeline valida que cada ciclo concluiu a quantidade de épocas solicitada, processou alvos e publicou ambos os arquivos, e registra train_bpb inicial/final, bytes-alvo, validação, F1, taxa de aprendizado e duração. O arquivo `feedback.txt` contém 40 perguntas históricas dedicadas exclusivamente ao replay supervisionado. O arquivo `validacao.txt` contém 20 perguntas novas, com formulações ausentes do treinamento, do feedback e do teste; somente esse holdout seleciona checkpoints. A validação independente é monitorada por pergunta ao longo das execuções. O teste reservado contém 12 perguntas e permanece intocado pelo treinamento. O teste reservado contém 12 perguntas sem sobreposição de perguntas ou respostas-alvo com o treino e é a medida mais forte de composição com respostas inéditas; o scheduler usa a validação, não o teste, para selecionar checkpoints. O benchmark também registra repetição consecutiva e diversidade de caracteres. O teste reservado inclui questões de composição e permanece fora do treinamento. O gradiente analítico é compartilhado entre treinamento e diagnóstico e possui testes de diferenças finitas para os principais blocos de parâmetros. A pergunta inteira participa do contexto e recebe gradientes pelos embeddings compartilhados. O estado recorrente é atualizado a cada byte da resposta; o gradiente retropropaga pelas últimas 64 transições temporais. A agregação da pergunta em 13 regiões posicionais contíguas condiciona o estado inicial da RNN; a média por região ainda comprime a ordem interna dos bytes. O executável limita o corpus a 512 MiB.

Comando:

morok-g2-tsukuyomi-train corpus.bin tsukuyomi-g2-v0.1.ckpt 10 42

Argumentos: arquivo de corpus, caminho do checkpoint, épocas opcionais e seed opcional.

## Auditoria matemática

A disposição dos 10.000 parâmetros é protegida por asserções estáticas encadeadas: cada bloco começa exatamente após o anterior e o último termina no limite do orçamento. Os testes comparam gradientes analíticos e diferenças finitas em representantes dos embeddings compartilhados de entrada/saída, projeção oculta, matriz recorrente compartilhada e projeção contextual da pergunta, biases, escala, deslocamento e estado inicial. Uma varredura adicional verifica diferenças finitas em offsets distribuídos por todo o intervalo de 10.000 parâmetros, com passo 137. Também verificam o gradiente dos embeddings da pergunta. Um teste independente confere o primeiro passo do Adam, as correções de bias dos momentos e o clipping pela norma global contra o valor numérico esperado. Outros testes cobrem acumulação em mini-lotes, retomada determinística, serialização dos momentos e weight decay seletivo.

Esses testes verificam a derivada da função atual em pontos representativos; não significam uma prova formal de todos os 10.000 valores para qualquer estado possível. O teste de diferenças finitas da matriz recorrente verifica a cadeia temporal em uma sequência curta. Um teste adicional compara atualizações com janela TBPTT de 1 e de 64 etapas. Outro teste verifica que uma perda após 32 bytes de resposta ainda alcança os pesos do contexto da pergunta, enquanto uma sequência com mais de 64 etapas respeita o truncamento. A janela TBPTT é limitada a 64 para permitir crédito de atribuição sobre respostas mais longas, com maior custo de memória e treinamento; dependências mais antigas influenciam o forward, mas não recebem gradiente direto além desse limite.

## Limites do ciclo de treinamento

- Até 1.000 épocas por tentativa.
- Até 100 tentativas/ciclos por execução (máximo teórico de 100.000 épocas).
- Até 20 perguntas de validação independente por ciclo, além de 20 perguntas exclusivas de feedback supervisionado.
- Treinamento contínuo desativado por padrão.
- Limites independentes validados tanto no workflow quanto no executor: `épocas <= 1000` e `tentativas <= 100`.
- O workflow de push só executa treinamento quando a mensagem do commit contém `[train-g2]`; os demais pushes apenas validam e compilam.
- Cada ciclo começa pelo melhor checkpoint e pelos momentos Adam correspondentes; o checkpoint persistido anterior é avaliado antes do treinamento e nunca é substituído por um estado inferior.

- Se ativado manualmente, o modo contínuo pode encerrar antes quando todas as questões selecionadas forem respondidas exatamente, mas nunca ultrapassa o limite de ciclos.
- O teste reservado de 12 perguntas é executado somente após o treinamento.

## Inferência
A geração prepara uma única vez as 13 regiões posicionais contíguas da pergunta e inicializa o estado recorrente. Cada byte gerado atualiza esse estado, que é reutilizado na previsão seguinte. Para prompts sem marcador U:, a preparação registra a ausência do contexto e evita repetir a busca pelo marcador.


A geração é autoregressiva e greedy. Cada byte gerado atualiza o estado oculto recorrente de 32 dimensões; a inferência mantém esse estado entre os passos. A CLI oferece modo batch: carrega o checkpoint uma vez e atende várias perguntas recebidas por stdin. A saída de resposta é encerrada no primeiro LF/CR/NUL ou no limite configurado (1 a 4.096 bytes); o workflow usa 160 bytes.

Comando:

morok-g2-tsukuyomi-generate tsukuyomi-g2-v0.1.ckpt "Olá" 120

A geração restringe transições para manter UTF-8 válido quando o prompt é UTF-8 válido e há espaço para concluir sequências multibyte. Isso garante codificação, não qualidade linguística. A API expõe decodificação gulosa padrão e opções de sampling com temperatura, top-k, top-p, seed fixa e penalidade de repetição; a configuração padrão continua determinística.


## Checkpoint

Formato binário MOROKG2T, versão 8, contagem explícita de 10.000 floats e checksum FNV-1a de 64 bits. O carregamento ocorre em estrutura temporária e só substitui o modelo ativo depois de validar o arquivo integral.

## Persistência entre execuções

O workflow mantém os arquivos `tsukuyomi-g2.ckpt` e `tsukuyomi-g2.ckpt.adam` na branch técnica `tsukuyomi-state`, fora da branch de código `main`. Antes de treinar, avalia o checkpoint persistido na validação independente e o define como piso de qualidade da execução. Cada ciclo restaura o melhor par pesos/Adam antes de atualizar os parâmetros; se o ciclo regredir, seus pesos e momentos são descartados e o ciclo seguinte parte do melhor estado. Ao final, somente um checkpoint que supera o estado anterior pode ser persistido. As execuções são serializadas. A branch também armazena `tsukuyomi-g2-feedback.txt` (memória corretiva derivada das 40 perguntas de feedback), `tsukuyomi-g2-learning-rate.txt` e `tsukuyomi-g2-question-history.json`. O learning rate é retomado entre execuções e decai 10% por ciclo. O histórico registra resposta gerada, acerto, regressão e recuperação por pergunta. O conjunto `feedback.txt` (40 perguntas) pode gerar exemplos supervisionados; `validacao.txt` (20 perguntas inéditas) nunca entra no treinamento e seleciona checkpoints. O teste reservado permanece separado.

## Limitações conhecidas do v0.1

- A pergunta é agregada em 13 regiões posicionais contíguas; a média ainda perde a ordem interna dos bytes dentro de cada região.
- Sem dropout: a versão 0.1 não aplica mascaramento estocástico; regularização fica restrita ao weight decay seletivo e clipping.
- Sem atenção, portas GRU/LSTM, memória externa ou subsistemas cognitivos G2. A recorrência atual usa candidato tanh e retenção sigmoide com matriz compartilhada.
- A máscara supervisionada seleciona os bytes de resposta; a pergunta condiciona o estado inicial por embeddings compartilhados e faixas posicionais, cuja compressão ainda perde detalhes de ordem.
- O pipeline atual usa entropia cruzada supervisionada; ainda não implementa Reinforcement Learning (RL) nem recompensa semântica.
- Sem tokenizer; a entrada continua byte-level, com categorias linguísticas auxiliares aprendidas a partir de embeddings compartilhados.
- O treinador aceita bytes arbitrários; a CLI de geração exige prompt UTF-8 válido para produzir continuação útil.
- O corpus supervisionado foi ampliado de 32 para 56 diálogos, preservando os 32 originais; o feedback possui 40 perguntas, a validação independente possui 20 perguntas novas e o teste reservado possui 12 perguntas. O conjunto continua pequeno e não equivale a um corpus natural amplo.
- O checkpoint de pesos (formato 8) e o arquivo `.adam` (formato 8) são artefatos separados e ambos são necessários para uma retomada exata do otimizador.

A versão 0.1 estabelece uma implementação executável, mensurável e isolada. Evoluções arquiteturais só devem ser aceitas com testes e comparação experimental.


## Etapa 6 — generalização e benchmark

A Etapa 6 não aumenta a capacidade nominal nem altera os pesos do inventário. O pipeline passa a registrar, além da igualdade lexical normalizada, F1 lexical por frequência, similaridade de caracteres por sequência de edição, repetição consecutiva e diversidade de caracteres. O relatório apresenta médias globais e resultados separados por habilidade (fato, conceito, composição e conversa). Similaridade e F1 continuam sendo diagnósticos: nenhuma delas aprova uma resposta incorreta como correta.

O score composto 70/15/15 é mantido como diagnóstico do relatório, não como critério primário de seleção. O scheduler e a escolha do melhor checkpoint usam ranking lexicográfico: número de respostas exatas primeiro; F1 e similaridade só desempatem em caso de igualdade de exatidão, com delta mínimo de 0,005. O learning rate decai 10% a cada ciclo e seu valor é persistido entre execuções, respeitando o piso de 1e-7. Foram adicionados testes de ablação causal: zerar a projeção contextual deve remover a influência da pergunta; zerar a matriz recorrente deve remover a influência do histórico da resposta. Esses testes verificam que os dois caminhos arquiteturais realmente contribuem para o estado final.

As 40 perguntas de feedback, as 20 perguntas novas de validação independente e as 12 perguntas de teste reservado são conjuntos distintos. Os 32 diálogos originais permanecem preservados. A avaliação final usa somente as 20 perguntas independentes para validação e as 12 perguntas reservadas para teste.


## Etapa 7 — scheduler orientado à generalização

O scheduler não usa a queda de train_bpb como evidência de melhora conversacional. O train_bpb continua sendo monitorado para diagnosticar ajuste ao corpus, mas a taxa de aprendizado e a seleção do melhor checkpoint são controladas pela validação. O ranking é lexicográfico: primeiro maximiza o número de respostas exatas; apenas quando esse número empata, considera a média de F1 lexical e similaridade de caracteres. O desempate exige ganho mínimo de 0,005 para evitar reagir a pequenas oscilações. Assim, respostas apenas parecidas nunca superam um checkpoint que responde corretamente a mais perguntas.

A cada ciclo concluído, o learning rate é reduzido em 10%, sem depender do resultado da validação, e nunca fica abaixo de 1e-7. O valor do scheduler é salvo e retomado entre execuções. O ranking lexicográfico continua selecionando o melhor checkpoint, mas não controla a taxa de aprendizado. A pontuação composta 70/15/15 permanece no relatório para comparação diagnóstica, mas não controla o ranking primário. Os pesos continuam limitados a 10.000 parâmetros; nenhum parâmetro, exemplo ou ciclo adicional foi introduzido.
