# Etapa 20 — Primeiro treinamento completo da G1

## Objetivo

Executar o primeiro ciclo completo e reproduzível de treinamento da G1, partindo de uma inicialização determinística e sem pesos pré-treinados. O treinamento usa o pipeline C++20 já implementado, mantém exatamente 1.000 parâmetros treináveis e deve produzir métricas, checkpoint retomável, melhor checkpoint de inferência e uma saída gerada pelo modelo treinado.

## Execução automatizada

O workflow `.github/workflows/g1-first-training.yml` compila em Release, executa toda a suíte CTest e executa `scripts/run-g1-first-training.sh`. O workflow é disparado quando os componentes da etapa 20 mudam em `main` e também pode ser iniciado manualmente.

Padrões da campanha técnica:
- 50 épocas solicitadas (configurável no workflow);
- semente 42;
- AdamW, learning rate 0,001;
- BPTT truncado em 64 bytes;
- early stopping com paciência 5;
- corpus `tests/fixtures/benchmark`, com divisão determinística por documento;
- retomada obrigatória a partir do checkpoint de fronteira de época;
- geração autoregressiva limitada a 128 bytes.

O script falha se o treinamento não produzir checkpoint, melhor modelo ou métricas; se a retomada não for confirmada pelo próprio treinador; ou se a geração não resultar em UTF-8 válido.

## Artefatos preservados

Cada execução publica um artifact do GitHub Actions contendo:
- `manifest.json`: configuração, commit, ambiente, proveniência e limitações;
- `metrics.jsonl`: métricas por época, atualizações, gradientes, BPB e eventos de checkpoint/retomada;
- `g1-resume.morokckp`: checkpoint de treinamento com estado do otimizador e RNG;
- `g1-best.morokckp`: melhor checkpoint para inferência;
- `generated.txt`: continuação produzida a partir de um prompt do fixture;
- `resource-usage.txt`: medição de recursos do processo no runner;
- arquivos SHA-256 dos dados e dos artefatos.

A seleção do melhor modelo é feita pela validação. O conjunto de teste é avaliado pelo treinador depois da seleção. O teste não é usado para escolher hiperparâmetros.

## Limites dos dados e interpretação

O repositório não contém um corpus natural real com proveniência/licença externa verificada. Para não inventar autorização nem incorporar dados de terceiros sem licença, a primeira execução automatizada usa somente o fixture técnico sintético versionado, identificado como CC0-1.0.

Essa execução comprova o ciclo de engenharia — inicialização, treinamento do zero, atualização dos parâmetros, avaliação, checkpoint, retomada e inferência — mas não comprova qualidade linguística, compreensão, generalização ou superioridade sobre o baseline. Os resultados devem ser reportados como experimento sintético, inclusive quando forem negativos.

A medição de memória do runner GitHub não comprova operação em hardware físico de 4 GB. Essa validação requer execução no equipamento de referência. Uma campanha científica final requer corpus natural com licença e proveniência verificáveis, splits fixos por documento e múltiplas sementes.

## Critérios de aceitação

- [x] Runner reproduzível versionado, com semente e hiperparâmetros explícitos.
- [x] Workflow automatizado para build Release, CTest e primeiro treinamento.
- [x] Treinamento parte de inicialização determinística sem pesos pré-treinados.
- [x] Métricas e eventos de treinamento são preservados em JSONL.
- [x] Checkpoint de retomada e melhor checkpoint de inferência são artefatos distintos.
- [x] Retomada é exercitada em uma segunda invocação do treinador.
- [x] Inferência pós-treinamento e validação UTF-8 são exercitadas.
- [x] Manifesto registra commit, ambiente, configuração, hash do corpus e limitações.
- [ ] Execução final do workflow no HEAD desta etapa aprovada.
- [ ] Campanha com corpus natural/licenciado executada e analisada.
- [ ] Consumo de memória medido em hardware físico com 4 GB de RAM.
- [ ] Validação final Debug/Release, ASan/UBSan e fuzzing aprovada no mesmo HEAD.

## Estado

A infraestrutura e o protocolo da primeira campanha completa da G1 estão versionados. A etapa só deve ser declarada integralmente validada depois de o workflow do HEAD final passar. A campanha sintética é a primeira execução técnica reproduzível, não substitui a campanha com corpus natural/licenciado nem a medição no hardware de 4 GB.
