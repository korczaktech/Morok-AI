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

- `morok-train-g2 <corpus-dir> <checkpoint-prefix> [epochs] [seed] [--resume]`
- `morok-generate-g2 <checkpoint-prefix> <prompt.txt> <saida.txt> [max-bytes]`

O treinamento ocorre sequencialmente para manter o uso de memória controlado. O argumento `--resume` retoma os checkpoints de treinamento dos dois especialistas de forma independente. Cada especialista usa a mesma partição determinística de documentos, mas mantém pesos e estados de otimizador próprios. Cada especialista usa o pipeline BPTT/AdamW ou SGD já testado pela G1. A G2 não importa pesos externos nem usa modelos pré-treinados.

## Workflow de treinamento\n\nO workflow `G2 First Complete Training` pode ser iniciado manualmente no GitHub Actions. Ele compila a aplicação, executa a suíte CTest, treina os dois especialistas, verifica a retomada de ambos, executa inferência e publica checkpoints, métricas, saída gerada, hashes e uso de recursos como artefato. O corpus usado pelo workflow é a fixture sintética CC0 do repositório.\n\n## Limites conhecidos

A geração atual é reranking de duas continuações independentes, não beam search conjunto nem atualização conjunta dos dois especialistas por uma única função de perda. As métricas devem ser reportadas por especialista e comparadas com a G1 em corpus e sementes equivalentes. A G2 só poderá ser considerada melhor após avaliação cega em dados não vistos, medição de RAM/latência e análise de diversidade/qualidade. A contagem de 2.000 parâmetros não implica melhoria automática.

## Corpus com um único documento

A G2 aceita um corpus contendo exatamente um arquivo `.txt` quando executada pelo treinador G2. Nesse modo, o documento inteiro é usado para treinamento e também para calcular as métricas de validação e teste. Essas métricas são avaliações sobre os dados vistos (resubstituição), não conjuntos independentes, e não demonstram generalização. Com três ou mais documentos únicos, permanece ativa a separação normal entre treinamento, validação e teste. O workflow de treinamento inicial e o bootstrap do Reinforcement-G2 usam o corpus exclusivo `tests/fixtures/g2-single-document`, que contém três pares controlados de pergunta e resposta no mesmo arquivo: céu, montanhas e grama. Esse corpus permite avaliar apenas essas três continuações observadas; não demonstra generalização.
