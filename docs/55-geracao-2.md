# Geração 2 — 2.000 parâmetros treináveis

## Decisão arquitetural

A G2 é uma arquitetura ensemble de dois especialistas recorrentes independentes, cada um usando o núcleo matemático G1 de 1.000 parâmetros. O total é exatamente 2.000 parâmetros treináveis, sem compartilhar pesos entre especialistas. A G1 permanece intacta e executável.

| Bloco | Arquitetura | Parâmetros |
|---|---|---:|
| Especialista A | RLAH G1 | 1.000 |
| Especialista B | RLAH G1 independente | 1.000 |
| **Total G2** | | **2.000** |

Cada especialista é inicializado com uma semente determinística diferente e treinado do zero no mesmo corpus, com partição determinística e estado de otimizador próprio. A inferência executa os dois especialistas em paralelo lógico. O score de um byte é a média das perdas dos dois; a geração produz uma hipótese por especialista e escolhe a continuação com menor perda média cruzada.

## Justificativa experimental

A G1 mostrou que a infraestrutura de treinamento pode executar atualizações, mas as respostas autônomas avaliadas ainda não demonstraram competência linguística. A G2 não presume que duplicar parâmetros resolva isso. A arquitetura de dois especialistas permite comparar diversidade de inicialização e reduzir dependência de uma única trajetória de treinamento, mantendo o núcleo G1 auditável.

## Persistência e compatibilidade

O checkpoint G2 é um par de arquivos com sufixos `.expert-a` e `.expert-b`, cada um usando o contêiner G1 versionado e validado. O carregador G2 exige metadados de geração e identidade do especialista. Checkpoints G1 não são aceitos como G2 por engano. Os checkpoints de treinamento de cada especialista permanecem separados para retomada.

## Treinamento e inferência

- `morok-train-g2 <corpus-dir> <checkpoint-prefix> [epochs] [seed]`
- `morok-generate-g2 <checkpoint-prefix> <prompt.txt> <saida.txt> [max-bytes]`

O treinamento ocorre sequencialmente para manter o uso de memória abaixo do limite operacional inicial de 4 GB. Cada especialista usa o pipeline BPTT/AdamW ou SGD já testado pela G1. A G2 não importa pesos externos nem usa modelos pré-treinados.

## Limites conhecidos

A geração atual é reranking de duas continuações independentes, não beam search conjunto nem atualização conjunta dos dois especialistas por uma única função de perda. As métricas devem ser reportadas por especialista e comparadas com a G1 em corpus e sementes equivalentes. A G2 só poderá ser considerada melhor após avaliação cega em dados não vistos, medição de RAM/latência e análise de diversidade/qualidade. A contagem de 2.000 parâmetros não implica melhoria automática.
