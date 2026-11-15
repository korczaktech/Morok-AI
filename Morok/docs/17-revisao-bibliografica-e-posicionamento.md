# 17. Revisão bibliográfica e posicionamento da RLAH

**Data de revisão:** 2026-11-13  
**Objetivo:** identificar precedentes e delimitar o que a RLAH pode afirmar.

## 17.1 Modelagem direta em bytes

**ByT5 — Xue et al., TACL 2022**  
Modelos byte-to-byte removem vocabulários subword fixos e oferecem robustez a ruído e flexibilidade multilíngue. O trabalho também destaca que sequências em bytes são mais longas e exigem avaliar FLOPs e velocidade.  
Fonte: https://aclanthology.org/2022.tacl-1.17/

**CANINE — Clark et al., TACL 2022**  
Opera diretamente em sequências de caracteres sem tokenização explícita e usa downsampling para reduzir comprimento antes do processamento contextual.  
Fonte: https://aclanthology.org/2022.tacl-1.5/

## 17.2 Segmentação aprendida

**Charformer — Tay et al., ICLR 2022**  
Apresenta Gradient-Based Subword Tokenization (GBST), que avalia blocos candidatos e aprende representações latentes de subpalavras a partir de caracteres. Isso estabelece precedente direto para segmentação aprendida.  
Fonte: https://research.google/pubs/charformer-fast-character-transformers-via-gradient-based-subword-tokenization/

**ByteFlow — Deng et al., ICLR 2026**  
Propõe segmentação de bytes guiada por compressão e custo de codificação latente, sem tokenizer fixo. É um precedente recente muito próximo da motivação de compressão adaptativa da RLAH.  
Fonte: https://proceedings.iclr.cc/paper_files/paper/2026/hash/eaf5d2cdb582c058a078d4fdf52a20f9-Abstract-Conference.html

**zip2zip — NeurIPS 2025**  
Usa compressão online LZW para criar unidades compactas adaptativas durante inferência, com embeddings dinâmicos; o método apresentado adapta um modelo já existente por fine-tuning, em vez de treinar um modelo inteiro do zero.  
Fonte: https://proceedings.neurips.cc/paper_files/paper/2025/hash/def682d57535b9e778508b4b1a911034-Abstract-Conference.html

## 17.3 Modelos recorrentes e de estado

**RWKV — Peng et al., Findings of EMNLP 2023**  
Combina propriedades de treinamento paralelizável e inferência recorrente, com complexidade de memória de inferência constante em relação ao comprimento do histórico, mantendo estado.  
Fonte: https://aclanthology.org/2023.findings-emnlp.936/

**Mamba — Gu e Dao, 2023/2024**  
Introduz SSM seletivo cujos parâmetros dependem da entrada, permitindo selecionar o que propagar ou esquecer, com processamento linear no comprimento da sequência.  
Fonte: https://arxiv.org/abs/2312.00752

**Mamba-3 — Lahoti et al., ICLR 2026**  
Investiga recorrência mais expressiva, estado complexo e MIMO sob uma perspectiva inference-first. Resultados em modelos de grande escala não podem ser extrapolados diretamente para G1, mas mostram que a forma da recorrência e o hardware real importam.  
Fonte: https://proceedings.iclr.cc/paper_files/paper/2026/hash/8abd2043b71a074278d5f687947bff9c-Abstract-Conference.html

## 17.4 Consequências para o Morok AI

1. Byte-level não é novo por si só.
2. Segmentação aprendida não é nova por si só.
3. Agrupamento guiado por compressão já possui precedentes, incluindo ByteFlow.
4. Inferência recorrente/linear já possui precedentes robustos.
5. A contribuição potencial da RLAH precisa ser delimitada como uma combinação e implementação específica, não como invenção de cada componente isolado.
6. A combinação fast/slow de baixo orçamento, agrupamento reversível hierárquico, avaliação multiobjetivo e controlador periódico poderá ser estudada, mas não está demonstrada como inédita.
7. A avaliação deve medir BPB, latência, throughput, memória, qualidade em dados fora do treino e custo de segmentação.

## 17.5 Plano de comparação

Ablations obrigatórias:
- núcleo fast/slow versus RNN simples;
- segmentação desligada versus ligada;
- uma escala versus três escalas;
- pesos fixos versus pesos adaptativos;
- saída binária versus softmax de 256 bytes;
- bytes diretos versus agrupamentos fixos;
- controlador com atualização periódica versus sem controlador.

Cada comparação deverá manter dados, divisão, semente e orçamento tão equivalentes quanto possível.
