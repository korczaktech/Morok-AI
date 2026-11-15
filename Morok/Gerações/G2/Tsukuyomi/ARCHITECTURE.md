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

O treinador percorre o corpus byte a byte e atualiza os parâmetros com gradientes da entropia cruzada, Adam, clipping global e weight decay. O relatório expõe loss em nats e train_bpb (bits por byte). A implementação v0.1 usa contexto finito e atualizações por alvo; não usa BPTT nem treinamento online. O executável limita o corpus a 512 MiB para manter o consumo de memória previsível.

Comando:

morok-g2-tsukuyomi-train corpus.bin tsukuyomi-g2-v0.1.ckpt 10 42

Argumentos: arquivo de corpus, caminho do checkpoint, épocas opcionais e seed opcional.

## Inferência

A geração é autoregressiva e greedy. Cada byte gerado passa a integrar o contexto das previsões seguintes. O limite máximo é obrigatório e limitado a 65.536 bytes na CLI.

Comando:

morok-g2-tsukuyomi-generate tsukuyomi-g2-v0.1.ckpt "Olá" 120

A geração restringe transições para manter UTF-8 válido quando o prompt é UTF-8 válido e há espaço para concluir sequências multibyte. Isso garante codificação, não qualidade linguística.

## Checkpoint

Formato binário MOROKG2T, versão 1, contagem explícita de 10.000 floats e checksum FNV-1a de 64 bits. O carregamento ocorre em estrutura temporária e só substitui o modelo ativo depois de validar o arquivo integral.

## Limitações conhecidas do v0.1

- Contexto limitado a 13 bytes anteriores e ao byte corrente.
- Sem atenção, recorrência, BPTT, memória externa ou subsistemas cognitivos G2.
- Sem tokenizer; a entrada é byte-level.
- O treinador aceita bytes arbitrários; a CLI de geração exige prompt UTF-8 válido para produzir continuação útil.
- Sem corpus natural embutido, benchmark linguístico ou alegação de compreensão.
- O checkpoint armazena pesos, mas não o estado do otimizador; retomada exata de treinamento ainda não está disponível.

A versão 0.1 estabelece uma implementação executável, mensurável e isolada. Evoluções arquiteturais só devem ser aceitas com testes e comparação experimental.
