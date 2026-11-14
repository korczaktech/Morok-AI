# G1 — Aprendizagem contínua com GitHub Actions

## Objetivo

Executar ciclos periódicos e retomáveis de treinamento da G1. O experimento inicial usa um pequeno corpus de português e um prompt fixo sobre a cor do céu. Cada ciclo restaura o checkpoint anterior, executa até 10 épocas adicionais, gera uma continuação e registra métricas e limitações.

Este workflow demonstra a infraestrutura de treinamento contínuo. Ele **não demonstra, por si só, compreensão de linguagem**.

## Agendamento e continuidade

O workflow `.github/workflows/training.yml`:

- executa a cada 15 minutos por `schedule`;
- aceita execução manual por `workflow_dispatch`;
- impede execuções simultâneas;
- restaura o artifact da última execução bem-sucedida;
- compila Release e executa CTest;
- treina até 10 épocas adicionais por ciclo, com teto total de 500;
- salva checkpoint, melhor modelo, métricas, geração e estado em artifact com retenção de 90 dias.

O checkpoint e o contador de épocas são transportados pelo artifact `morok-g1-continual-state`. O workflow não grava pesos no repositório e não cria commits automáticos. A agenda do GitHub Actions é periódica, não um processo sempre ativo, e pode atrasar.

## Dados e avaliação atual

Os documentos `doc-*.txt` em `tests/fixtures/continual/` contêm um par pergunta/resposta por documento. Isso torna cada fim de documento um limite de resposta para o treinamento EOS. O corpus começou com 12 exemplos sobre o céu e recebeu exemplos factuais adicionais em outros tópicos; continua sendo um conjunto pequeno e controlado, não uma amostra representativa da língua portuguesa.

A métrica principal do treinador é a perda de predição do próximo byte (BPB) em treino/validação/teste. Ela mede qualidade estatística de predição no corpus, não compreensão semântica.

Depois de cada ciclo, `morok_generate` recebe o prompt versionado e produz até 128 bytes. O script `scripts/evaluate-g1-qa.py` também avalia perguntas reservadas em `tests/fixtures/continual-eval/cases.json`, com paráfrases e tópicos factuais distintos, registrando exact match e token F1. Ela **não** encerra o treinamento, não cria marcador de sucesso e não altera o estado do workflow para `goal_reached`.

O estado final possível do orçamento é `budget_exhausted`. Isso significa somente que o limite de épocas foi atingido. O status da avaliação semântica permanece `not_established` até existir um conjunto independente de perguntas e respostas, com critérios de aceitação que não sejam uma simples busca por substring.

## Contagem de parâmetros

A arquitetura armazena exatamente 1.000 parâmetros FP32. O caminho de treinamento B0 atualiza os 1.000 parâmetros. Os 29 parâmetros `v` e `theta_len` formam a cabeça EOS e recebem gradientes de continuação e fim de documento.

Essa distinção é registrada em `status.json`:
- `architecture_parameters: 1000`;
- `trainable_parameters: 1000` para o caminho B0, incluindo a cabeça EOS.

O limite arquitetural de 1.000 é preservado. Mais épocas não aumentam a capacidade estrutural do modelo.

## Artefatos

O artifact de cada execução contém, quando produzidos:

- `g1-continual-resume.morokckp`: estado retomável;
- `g1-continual-best.morokckp`: melhor modelo segundo a validação interna;
- `metrics.jsonl` e `cycle-summary.jsonl`: métricas e resumo;
- `generated.txt`: continuação gerada;
- `completed-epochs.txt`: contador acumulado;
- `status.json`: estado, hash do corpus, diagnóstico lexical e limitações;
- `qa-evaluation.json`: resultados lexicais das perguntas reservadas para avaliação.

## Limites conhecidos

- 1.000 parâmetros arquiteturais são uma capacidade muito pequena para linguagem geral.
- A entrada e a saída são processadas byte a byte; o estado recorrente é pequeno.
- A geração tem uma cabeça EOS aprendida a partir de fronteiras de documentos, além do limite rígido de bytes. A qualidade do critério de parada depende da representatividade dos documentos de treinamento.
- O corpus é pequeno e o modelo pode memorizar exemplos e prompt.
- A métrica BPB não substitui avaliação semântica.
- O critério lexical `azul` é diagnóstico, não evidência de compreensão.
- Runner hospedado não comprova execução em hardware físico com 4 GB de RAM.

## Critérios de aceitação da infraestrutura

- [x] Workflow agendado e execução manual.
- [x] Checkpoint transportado entre execuções por artifact.
- [x] Retomada com orçamento incremental de épocas.
- [x] Métricas de predição de bytes, geração pós-treinamento e diagnóstico lexical em perguntas reservadas.
- [x] Limite total de épocas.
- [x] Busca lexical rebaixada a diagnóstico, sem declarar sucesso semântico.
- [x] Treinar os 29 parâmetros da cabeça EOS em exemplos de continuação e fim de documento.
- [x] Implementar geração com amostragem controlada e critério de parada treinado.
- [ ] Ampliar corpus natural, diversificado e licenciado.
- [ ] Criar avaliação semântica independente com revisão humana; o atual exact match/token F1 é somente lexical.
- [ ] Validar em hardware físico com 4 GB de RAM.

## Decodificação da G1

A API de geração agora aceita controles de decodificação e parada:

- `temperature = 0`: preserva a escolha gulosa determinística anterior;
- `temperature > 0`: amostra entre candidatos conforme a distribuição de perdas do modelo;
- `top_k`: limita a amostragem aos candidatos com menor perda; zero considera todos os candidatos UTF-8 válidos;
- `seed`: torna a amostragem reproduzível;
- `repetition_penalty` e `repetition_window`: acrescentam custo a bytes ASCII imprimíveis repetidos na janela recente;
- `eos_threshold` e `min_new_bytes`: controlam o limiar de parada aprendido e o mínimo de bytes gerados.

Esses controles mudam a estratégia de decodificação, não aumentam a capacidade aprendida. A penalidade de repetição é heurística e não deve ser interpretada como correção semântica. A CLI `morok_generate` também expõe `--temperature`, `--top-k`, `--seed`, `--repetition-penalty`, `--repetition-window`, `--eos-threshold` e `--min-new-bytes`. O argumento posicional opcional `max_bytes` continua compatível com a interface anterior.

## EOS aprendido

Os 29 parâmetros finais do inventário agora são uma cabeça de parada:
- 22 pesos aplicados ao estado recorrente combinado;
- sete vieses selecionados por faixa logarítmica de comprimento.

Durante BPTT, cada contexto que possui um próximo byte recebe o rótulo "continuar". Depois do último byte de cada documento de treinamento, o loop consome esse byte e aplica um rótulo positivo de parada. A cabeça EOS é atualizada pelo mesmo otimizador dos demais parâmetros. A inferência consulta a probabilidade de parada depois do comprimento mínimo e somente em fronteiras completas de caracteres UTF-8.

O identificador de arquitetura do checkpoint mudou para `RLAH-G1-fastslow-d11-bit8-eos`. O ciclo contínuo descarta checkpoints anteriores sem esse identificador e reinicia deterministicamente, pois os 29 valores mudaram de semântica. Checkpoints antigos não são compatíveis com essa versão.
