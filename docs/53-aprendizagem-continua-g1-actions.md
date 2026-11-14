# G1 — Aprendizagem contínua com GitHub Actions

## Objetivo

Executar ciclos periódicos e retomáveis de treinamento da G1, usando um pequeno corpus de paráfrases em português para a pergunta "Que cor é o céu?". Cada ciclo restaura o checkpoint do ciclo anterior, executa até 10 épocas adicionais, gera uma continuação para um prompt fixo e registra o estado.

O workflow não pretende enumerar todos os sinônimos ou paráfrases possíveis. O corpus contém exemplos humanos fixos, e o critério automatizado é intencionalmente estreito.

## Agendamento e continuidade

O workflow `.github/workflows/training.yml`:

- executa a cada 15 minutos por `schedule`;
- também aceita execução manual por `workflow_dispatch`;
- impede execuções simultâneas com um grupo de concorrência;
- restaura o artifact da última execução bem-sucedida;
- compila a versão Release e executa CTest;
- treina mais 10 épocas por ciclo, com teto total de 500 épocas;
- salva checkpoint, melhor modelo, métricas, geração e estado em um artifact com retenção de 90 dias.

O checkpoint e o contador de épocas são transportados entre execuções pelo artifact `morok-g1-continual-state`. O workflow não grava pesos no repositório e não cria commits automáticos.

## Dados

Os arquivos em `tests/fixtures/continual/` contêm variações escritas para este experimento, por exemplo:

- "O céu é azul."
- "Azul."
- "Com certeza, é azul."
- "A cor do céu é azul."
- "O céu apresenta uma coloração azul."
- "O céu tem aparência azul."

São paráfrases e respostas relacionadas, não uma lista matematicamente completa de sinônimos. O conjunto é pequeno e serve como fixture técnico; não é um corpus representativo de português.

## Critério de parada

Depois de cada ciclo, `morok_generate` recebe o prompt versionado e produz até 128 bytes. Um verificador lexical procura o token "azul" na continuação e rejeita um caso simples de negação próxima.

Se o token for encontrado, o estado `goal_reached` é persistido e os ciclos seguintes deixam de compilar ou treinar. Se não for encontrado, o próximo ciclo agendado retoma o checkpoint. Ao atingir 500 épocas sem satisfazer o critério, o estado passa a `budget_exhausted` e os ciclos param.

**Limitação essencial:** encontrar a palavra "azul" não comprova que o modelo compreendeu a pergunta ou produziu uma resposta semanticamente correta. É apenas um teste lexical automatizado para demonstrar a mecânica de continuidade e parada. A avaliação semântica confiável exigirá um conjunto de testes independente e um avaliador validado, preferencialmente com revisão humana.

## Artefatos

O artifact de cada execução contém, quando produzidos:

- `g1-continual-resume.morokckp`: estado retomável do treinamento;
- `g1-continual-best.morokckp`: melhor modelo segundo a validação interna do treinador;
- `metrics.jsonl` e `cycle-summary.jsonl`: métricas e resumo;
- `generated.txt`: continuação gerada;
- `completed-epochs.txt`: contador acumulado;
- `status.json`: estado, hash do corpus e limitações;
- `goal-reached.txt`: marcador terminal quando o critério lexical é satisfeito.

## Limites e riscos

- A G1 permanece com exatamente 1.000 parâmetros; mais épocas não aumentam sua capacidade estrutural.
- O modelo pode memorizar o corpus e o prompt fixo.
- O critério lexical não mede compreensão, generalização nem diversidade de paráfrases.
- A agenda do GitHub Actions é periódica, não um processo permanentemente ativo; o horário pode atrasar.
- Os artifacts têm retenção limitada. Se o estado deixar de estar disponível, uma execução sem checkpoint inicia novamente.
- O orçamento é limitado a 500 épocas para evitar execução indefinida e consumo ilimitado de minutos.
- A execução em runner hospedado não comprova operação em hardware físico com 4 GB de RAM.

## Critérios de aceitação da infraestrutura

- [x] Workflow agendado e execução manual.
- [x] Corpus de paráfrases versionado.
- [x] Checkpoint transportado entre execuções por artifact.
- [x] Retomada com orçamento incremental de épocas.
- [x] Geração pós-treinamento e critério de parada lexical.
- [x] Limite total de épocas e parada persistente.
- [ ] Primeira execução do workflow aprovada no GitHub Actions.
- [ ] Avaliação semântica independente da resposta gerada.
