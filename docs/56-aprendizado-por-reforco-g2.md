# Reinforcement-G2

O workflow aplica treinamento e avaliação exclusivamente à G2. A G2 contém dois especialistas independentes de 1.000 parâmetros cada, totalizando exatamente 2.000 parâmetros treináveis.

## Isolamento

- Checkpoints G2 usam o formato pareado dos especialistas A e B.
- O histórico fica em `data/reinforcement-g2/answers.json`, separado dos episódios da G1.
- O workflow é `.github/workflows/reinforcement-g2.yml`.
- A geração usa `morok-generate-g2`; as atualizações usam `morok-reinforce-g2`.
- O artefato do Actions preserva os checkpoints e resultados da execução.

## Corpus e perguntas

O bootstrap usa exatamente um arquivo de texto em `tests/fixtures/g2-single-document/doc-01.txt`. Esse arquivo contém três pares controlados de pergunta e resposta:

- `Usuario: O ceu e azul?` → `Sim, o ceu e azul`.
- `Usuario: As montanhas sao altas ou baixas?` → `As montanhas sao altas.`.
- `Usuario: Que cor e a grama?` → `A grama e verde.`.

Os três prompts no banco de feedback correspondem aos prefixos literais do corpus. O bootstrap é executado separadamente para os dois especialistas, com até 100 épocas. Como há apenas um documento, a métrica de seleção é calculada sobre o próprio documento de treinamento; isso não constitui validação independente nem demonstra generalização.

## Ciclo de tentativa e correção

Cada tentativa gera uma resposta e a avalia com um verificador lexical limitado às três perguntas controladas.

- Se a resposta for correta e não degenerada, ela própria é usada como sequência-alvo para uma atualização positiva.
- Se a resposta estiver errada ou for degenerada, ela nunca é usada como alvo positivo. O sistema usa a resposta de referência correspondente ao corpus como demonstração-oráculo.
- Cada demonstração-oráculo aplica 25 passos de atualização positiva aos dois especialistas. A tentativa seguinte é gerada com os pesos já atualizados.
- O banco registra se a atualização usou a resposta do modelo ou uma demonstração-oráculo.

Esse mecanismo permite que o modelo receba um sinal de correção após errar. Como a resposta de referência é fornecida pelo sistema, essa etapa é treinamento supervisionado guiado por oráculo; não deve ser descrita como aprendizado autônomo nem como REINFORCE completo. A implementação usa uma aproximação reward-weighted BPTT.

## Gate comportamental e limites

O workflow só passa no gate comportamental se pelo menos uma resposta gerada durante a execução for correta e não degenerada. Se nenhuma passar, o job termina como falha mesmo que compilação e testes de software estejam verdes.

Cada execução começa com bootstrap limpo a partir do corpus versionado; os pesos treinados em uma execução não são restaurados automaticamente na execução seguinte. Os artefatos preservam os resultados e checkpoints para inspeção, mas não implicam persistência automática entre runs.

O avaliador lexical só mede as três perguntas definidas. Acertar esses exemplos não comprova compreensão geral, capacidade de responder perguntas inéditas ou competência linguística ampla. A quantidade de parâmetros também não é evidência de qualidade por si só.

## Execução

O workflow não usa cron. Pode ser iniciado manualmente, com caixas de seleção para as três perguntas, quantidade de tentativas e épocas de bootstrap, ou por mudanças relevantes no código. É necessário selecionar pelo menos uma pergunta. O limite de tentativas por execução é 10.
