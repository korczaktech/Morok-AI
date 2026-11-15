# Tsukuyomi G2 — arquitetura v0.1

## Identidade

- Geração: G2
- Modelo: Tsukuyomi
- Versão: 0.1.0
- Perfil: maior capacidade da G2
- Orçamento exato: 10.000 parâmetros treináveis
- Vocabulário: 256 valores de byte
- Dimensão oculta: 32
- Contexto causal: até 13 bytes anteriores, além do byte corrente
- Implementação: C++20, sem pesos pré-treinados e sem dependência de modelos externos

## Arquitetura

O v0.1 é um modelo neural autoregressivo de bytes com embeddings de entrada/saída compartilhados. O byte anterior é convertido em um vetor de 32 dimensões; os 13 bytes anteriores a ele entram por uma projeção posicional numérica. A soma passa por uma projeção oculta, tangente hiperbólica e transformação afim aprendida. A cabeça de saída calcula 256 logits usando a própria matriz de embeddings, compartilhada com a entrada.

A previsão é causal: ao prever o byte na posição t, o modelo só recebe bytes anteriores a t. A primeira previsão usa um vetor inicial treinável. A distribuição de saída usa softmax estável e a função objetivo é entropia cruzada categórica.

## Inventário auditável

| Bloco | Forma | Parâmetros |
|---|---:|---:|
| Embeddings compartilhados entrada/saída | 256 × 32 | 8.192 |
| Projeção oculta | 32 × 32 | 1.024 |
| Projeção do contexto | 13 × 32 | 416 |
| Bias oculto | 32 | 32 |
| Bias de saída | 256 | 256 |
| Escala oculta | 32 | 32 |
| Deslocamento oculto | 32 | 32 |
| Estado inicial | 16 | 16 |
| **Total** | | **10.000** |

Os offsets e a soma são verificados por asserção estática no código. O checkpoint rejeita versões, contagens, checksums e valores não finitos incompatíveis.

## Treinamento

O treinador percorre o corpus byte a byte e atualiza os parâmetros com gradientes da entropia cruzada, Adam, clipping global e weight decay. Em corpora com pares U:/IA:, o executável cria uma máscara que seleciona somente os bytes posteriores ao prefixo IA: como alvos de treinamento; os bytes da pergunta continuam disponíveis como contexto. Em arquivos sem pares de diálogo reconhecidos, todos os bytes continuam sendo alvos. Os gradientes são acumulados em mini-lotes (16 alvos por padrão) e aplicados uma vez por lote. O estado do Adam (momentos de primeira e segunda ordem e contador global de atualizações) é serializado separadamente e restaurado na retomada. O learning rate padrão é 0,001 (0,1%), constante e sem scheduler; Adam usa β1=0,9, β2=0,999 e epsilon=1e-8. O clipping limita a norma global a 1,0. O weight decay padrão é 0,00001 e atua somente em embeddings e matrizes, não em biases, escala, deslocamento ou estado inicial. O relatório expõe loss e train_bpb calculados sobre os alvos selecionados. O gradiente analítico é compartilhado entre treinamento e diagnóstico e possui testes de diferenças finitas para os principais blocos de parâmetros. A implementação ainda usa contexto finito de 14 bytes e não propaga gradientes através do tempo (sem BPTT). O executável limita o corpus a 512 MiB.

Comando:

morok-g2-tsukuyomi-train corpus.bin tsukuyomi-g2-v0.1.ckpt 10 42

Argumentos: arquivo de corpus, caminho do checkpoint, épocas opcionais e seed opcional.

## Inferência

A geração é autoregressiva e greedy. Cada byte gerado passa a integrar o contexto das previsões seguintes. A CLI oferece modo batch: carrega o checkpoint uma vez e atende várias perguntas recebidas por stdin. A saída de resposta é encerrada no primeiro LF/CR/NUL ou no limite configurado (1 a 4.096 bytes); o workflow usa 160 bytes.

Comando:

morok-g2-tsukuyomi-generate tsukuyomi-g2-v0.1.ckpt "Olá" 120

A geração restringe transições para manter UTF-8 válido quando o prompt é UTF-8 válido e há espaço para concluir sequências multibyte. Isso garante codificação, não qualidade linguística.

## Checkpoint

Formato binário MOROKG2T, versão 1, contagem explícita de 10.000 floats e checksum FNV-1a de 64 bits. O carregamento ocorre em estrutura temporária e só substitui o modelo ativo depois de validar o arquivo integral.

## Limitações conhecidas do v0.1

- Contexto limitado a 13 bytes anteriores e ao byte corrente (14 bytes efetivos).
- Sem dropout: a versão 0.1 não aplica mascaramento estocástico; regularização fica restrita ao weight decay seletivo e clipping.
- Sem atenção, recorrência, BPTT, memória externa ou subsistemas cognitivos G2.
- A máscara supervisionada seleciona os bytes de resposta, mas não amplia o contexto nem ensina dependências entre partes distantes da pergunta.
- O pipeline atual usa entropia cruzada supervisionada; ainda não implementa Reinforcement Learning (RL) nem recompensa semântica.
- Sem tokenizer; a entrada é byte-level.
- O treinador aceita bytes arbitrários; a CLI de geração exige prompt UTF-8 válido para produzir continuação útil.
- Sem corpus natural embutido, benchmark linguístico ou alegação de compreensão.
- O checkpoint de pesos e o arquivo `.adam` são artefatos separados e ambos são necessários para uma retomada exata do otimizador.

A versão 0.1 estabelece uma implementação executável, mensurável e isolada. Evoluções arquiteturais só devem ser aceitas com testes e comparação experimental.
