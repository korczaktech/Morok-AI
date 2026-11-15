# Tsukuyomi G2 — arquitetura v0.1

## Identidade

- Geração: G2
- Modelo: Tsukuyomi
- Versão: 0.1.0
- Perfil: maior capacidade da G2
- Orçamento exato: 10.000 parâmetros treináveis
- Vocabulário: 256 valores de byte
- Dimensão oculta: 32
- Contexto causal: byte corrente, pergunta agregada em 13 faixas e até 13 bytes anteriores da resposta
- Implementação: C++20, sem pesos pré-treinados e sem dependência de modelos externos

## Arquitetura

O v0.1 é um modelo neural autoregressivo de bytes com embeddings de entrada/saída compartilhados. O byte corrente é convertido em um vetor de 32 dimensões. Em prompts U:/IA:, todos os bytes da pergunta são convertidos pelos embeddings compartilhados e agregados em 13 faixas posicionais de 16 dimensões; a projeção da pergunta ocupa a metade inferior dos 416 pesos contextuais. Isso preserva identidade de bytes e posição aproximada, embora ainda haja colisões dentro das faixas. Durante a resposta, até 13 bytes anteriores da própria resposta entram em um canal separado de 16 dimensões na metade superior dos pesos contextuais, com escala 0,025 para aproximar a magnitude inicial dos embeddings e reduzir a dominância dos bytes crus. Os dois canais não compartilham pesos, embora se encontrem na projeção oculta de 32 dimensões. Assim, previsões deixam de depender somente do último byte: o modelo passa a receber uma janela curta da sequência que está produzindo. A soma passa por uma projeção oculta, tangente hiperbólica e transformação afim aprendida. A cabeça de saída calcula 256 logits usando a própria matriz de embeddings, compartilhada com a entrada.

A previsão é causal: ao prever o byte na posição t, o modelo só recebe bytes anteriores a t. A primeira previsão usa um vetor inicial treinável. A distribuição de saída usa softmax estável e a função objetivo é entropia cruzada categórica.

## Inventário auditável

| Bloco | Forma | Parâmetros |
|---|---:|---:|
| Embeddings compartilhados entrada/saída | 256 × 32 | 8.192 |
| Projeção oculta | 32 × 32 | 1.024 |
| Projeção do contexto | 13 × 16 pergunta + 13 × 16 resposta | 416 |
| Bias oculto | 32 | 32 |
| Bias de saída | 256 | 256 |
| Escala oculta | 32 | 32 |
| Deslocamento oculto | 32 | 32 |
| Estado inicial | 16 | 16 |
| **Total** | | **10.000** |

Os offsets e a soma são verificados por asserção estática no código. O checkpoint rejeita versões, contagens, checksums e valores não finitos incompatíveis. A versão 4 do formato interno impede carregar pesos treinados com a representação contextual anterior; a identidade pública do Tsukuyomi continua 0.1.

## Treinamento

O treinador percorre os bytes-alvo e atualiza os parâmetros com gradientes da entropia cruzada, Adam, clipping global e weight decay. Os índices dos alvos são embaralhados deterministicamente a cada época, sem embaralhar o texto: cada previsão mantém seu contexto causal original. A semente depende do contador Adam, preservando a sequência de atualizações ao retomar checkpoints. Em corpora com pares U:/IA:, o executável cria uma máscara que seleciona somente os bytes posteriores ao prefixo IA: como alvos de treinamento; os bytes da pergunta continuam disponíveis como contexto. Em arquivos sem pares de diálogo reconhecidos, todos os bytes continuam sendo alvos. Os gradientes são acumulados em mini-lotes (16 alvos por padrão) e aplicados uma vez por lote. O estado do Adam (momentos de primeira e segunda ordem e contador global de atualizações) é serializado separadamente e restaurado na retomada. A versão 3 desse arquivo invalida momentos calculados antes da separação dos canais de pergunta e resposta. O learning rate inicial é 0,001 (0,1%); o executor cíclico o reduz pela metade após cinco ciclos sem melhora da pontuação de validação (80% exatidão + 20% F1 lexical médio), até 0,00005; Adam usa β1=0,9, β2=0,999 e epsilon=1e-8. O clipping limita a norma global a 1,0. O weight decay padrão é 0,00001 e atua somente em embeddings e matrizes, não em biases, escala, deslocamento ou estado inicial. O relatório expõe loss e train_bpb calculados sobre os alvos selecionados. A validação padrão cobre as 40 perguntas, sem escolher respostas que já aparecem no treino; o scheduler usa a qualidade das respostas geradas. O benchmark também registra repetição consecutiva e diversidade de caracteres. O teste reservado inclui questões de composição e permanece fora do treinamento. O gradiente analítico é compartilhado entre treinamento e diagnóstico e possui testes de diferenças finitas para os principais blocos de parâmetros. A pergunta inteira participa do contexto e recebe gradientes pelos embeddings compartilhados. Os bytes anteriores da resposta são fornecidos como contexto causal local e atualizam a projeção contextual. A agregação da pergunta em 13 faixas é uma compressão com colisões, não uma memória sequencial exata. A geração não propaga gradientes através dos bytes gerados (sem BPTT). O executável limita o corpus a 512 MiB.

Comando:

morok-g2-tsukuyomi-train corpus.bin tsukuyomi-g2-v0.1.ckpt 10 42

Argumentos: arquivo de corpus, caminho do checkpoint, épocas opcionais e seed opcional.

## Inferência
A geração prepara uma única vez as 13 faixas da pergunta imutável e as reutiliza durante a geração autoregressiva. A cada passo, os bytes anteriores da resposta são incorporados à janela causal; assim, o contexto da pergunta é cacheado, mas o histórico da resposta é atualizado. Para prompts sem marcador U:, a preparação registra a ausência do contexto e evita repetir a busca pelo marcador.


A geração é autoregressiva e greedy. Cada byte gerado passa a integrar a janela causal de até 13 bytes usada nas previsões seguintes. A CLI oferece modo batch: carrega o checkpoint uma vez e atende várias perguntas recebidas por stdin. A saída de resposta é encerrada no primeiro LF/CR/NUL ou no limite configurado (1 a 4.096 bytes); o workflow usa 160 bytes.

Comando:

morok-g2-tsukuyomi-generate tsukuyomi-g2-v0.1.ckpt "Olá" 120

A geração restringe transições para manter UTF-8 válido quando o prompt é UTF-8 válido e há espaço para concluir sequências multibyte. Isso garante codificação, não qualidade linguística.


A inferência prepara uma única vez as 13 faixas da pergunta imutável e as reutiliza durante a geração autoregressiva. Isso elimina a varredura e a agregação repetidas da pergunta a cada byte gerado; não altera pesos, logits ou o orçamento de parâmetros. Para prompts sem marcador U:, a preparação registra a ausência do contexto e também evita repetir a busca pelo marcador.

## Checkpoint

Formato binário MOROKG2T, versão 4, contagem explícita de 10.000 floats e checksum FNV-1a de 64 bits. O carregamento ocorre em estrutura temporária e só substitui o modelo ativo depois de validar o arquivo integral.

## Limitações conhecidas do v0.1

- A pergunta inteira é agregada em 13 faixas posicionais; a compressão perde detalhes de ordem quando várias posições caem na mesma faixa.
- Sem dropout: a versão 0.1 não aplica mascaramento estocástico; regularização fica restrita ao weight decay seletivo e clipping.
- Sem atenção, recorrência, BPTT, memória externa ou subsistemas cognitivos G2.
- A máscara supervisionada seleciona os bytes de resposta; a pergunta inteira participa do contexto por embeddings compartilhados e faixas posicionais, mas a agregação não representa todas as dependências de ordem.
- O pipeline atual usa entropia cruzada supervisionada; ainda não implementa Reinforcement Learning (RL) nem recompensa semântica.
- Sem tokenizer; a entrada é byte-level.
- O treinador aceita bytes arbitrários; a CLI de geração exige prompt UTF-8 válido para produzir continuação útil.
- O corpus supervisionado foi ampliado de 32 para 56 diálogos, preservando os 32 originais; a validação mantém 40 perguntas e o teste reservado foi ampliado para 12 perguntas. O conjunto continua pequeno e não equivale a um corpus natural amplo.
- O checkpoint de pesos e o arquivo `.adam` são artefatos separados e ambos são necessários para uma retomada exata do otimizador.

A versão 0.1 estabelece uma implementação executável, mensurável e isolada. Evoluções arquiteturais só devem ser aceitas com testes e comparação experimental.
