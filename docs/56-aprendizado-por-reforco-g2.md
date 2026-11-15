# Reinforcement-G2

A G2 possui dois especialistas independentes de 1.000 parâmetros cada, totalizando exatamente 2.000 parâmetros treináveis.

## Componentes

- Checkpoints de inferência: `<prefix>.expert-a` e `<prefix>.expert-b`.
- Checkpoints de retomada do bootstrap: `<prefix>.expert-a.training` e `<prefix>.expert-b.training`.
- Histórico: `data/reinforcement-g2/answers.json`.
- Runner: `scripts/run-g2-reinforcement.py`.
- Gerador: `morok-generate-g2`.
- Atualizador: `morok-reinforce-g2`.

## Corpus

O bootstrap usa três documentos independentes em `tests/fixtures/g2-single-document/`, um por pergunta e resposta. Cada arquivo termina exatamente no fim da resposta, permitindo que o treinador aplique um alvo EOS positivo em cada limite:

- `Usuario: O ceu e azul?` → `Sim, o ceu e azul`.
- `Usuario: As montanhas sao altas ou baixas?` → `As montanhas sao altas.`.
- `Usuario: Que cor e a grama?` → `A grama e verde.`.

O modo G2 treina nos três documentos e calcula as métricas sobre os mesmos documentos. Isso permite treinar o encerramento em cada resposta, mas as métricas são de resubstituição, não validação independente, e não medem generalização.

## Correções de aprendizado

O otimizador minimiza a perda subtraindo o gradiente. Portanto, recompensa positiva deve aplicar o gradiente BPTT sem inverter seu sinal; recompensa negativa deve inverter o sinal. O atualizador G2 segue essa convenção. Ele usa AdamW, clipping de norma 5 e taxa padrão de 0,001. O número padrão de passos por atualização de episódio é 1.000, e o bootstrap padrão usa 1.000 épocas (configuráveis). O atualizador mede a perda média da sequência-alvo antes e depois da atualização para os dois especialistas. Reduzir essa perda demonstra ajuste ao exemplo, não generalização.

O teste unitário compara a perda da sequência antes e depois da atualização para os dois especialistas. Ele exige que recompensa positiva reduza a perda e que o gradiente invertido aumente a perda. Isso valida a direção da atualização, não apenas a alteração dos parâmetros. O runner usa demonstrações de referência com recompensa positiva quando a resposta está errada e gera novamente após a atualização. O gate comportamental avalia essa nova saída, nunca a amostra anterior ao treinamento. Portanto, esse fluxo é aprendizado supervisionado guiado por oráculo, não REINFORCE clássico.

Quando a resposta gerada está errada ou degenerada, o runner usa a resposta de referência como demonstração-oráculo. Essa correção é treinamento supervisionado guiado por referência, não aprendizado autônomo nem uma implementação completa de REINFORCE.

## Geração e avaliação

O executável de geração G2 ativa saída imprimível, penalização de repetição e um limite de bytes. Essas medidas reduzem saídas degeneradas, mas não garantem uma resposta semanticamente correta. O workflow registra a resposta inicial e a resposta pós-atualização separadamente para permitir auditar o efeito do reforço.

O avaliador compara a sequência completa de tokens normalizados com a resposta de referência de cada pergunta. A normalização ignora diferenças de acentuação, caixa e pontuação, mas não ignora palavras extras, repetições ou sufixos aleatórios. Assim, uma saída como a resposta correta seguida de ruído é incorreta. Respostas incompletas, negativas, com tokens inesperados ou degeneradas não recebem recompensa positiva.

Na inicialização, o runner reconcilia os resultados históricos com o avaliador estrito e recalcula os totais. Experiências antigas cuja resposta inicial do modelo contém tokens extras não são reaproveitadas como alvo do próprio modelo: o replay usa a demonstração de referência para evitar consolidar ruído histórico.

## Persistência

O runner não apaga mais os checkpoints de inferência a cada execução. Se os dois checkpoints de especialistas já existirem, o treinamento continua a partir desses pesos. Se ambos não existirem, o runner faz o bootstrap; se existir apenas um, interrompe com erro para evitar um par inconsistente.

Os checkpoints de treinamento do bootstrap também podem ser retomados com `--resume` quando ambos estiverem disponíveis. O banco de episódios permanece separado dos pesos do modelo.

### Memória persistente de experiências

Antes de gerar novas respostas, o runner consulta os episódios persistidos e reapresenta ao atualizador os alvos distintos mais recentes. Experiências duplicadas com a mesma pergunta, prompt e resposta-alvo são consolidadas, mantendo a ocorrência mais recente, para evitar sobreajuste por replay repetitivo. A memória é limitada por `--memory-replay-limit` (padrão 32; zero desativa) e cada exemplo usa 25 passos por padrão, configuráveis por `--memory-replay-steps`. Isso acontece mesmo quando a execução atual solicita apenas uma tentativa e uma época de bootstrap.

Experiências cuja atualização original usou uma resposta correta do próprio modelo são reproduzidas com essa resposta inicial somente se ela passar novamente pelo avaliador estrito. Caso contrário, são recuperadas com a resposta de referência, em vez de reforçar uma saída incorreta ou contaminada por ruído. O runner registra quantas experiências distintas foram consideradas, quantas atualizações de replay reduziram a perda e quantas foram ignoradas. Respostas que já estão corretas dispensam uma nova atualização; quando a perda-alvo já convergiu, o episódio é registrado como atualização ignorada em vez de falhar o workflow. Episódios interrompidos com estado pendente são reconciliados como ignorados na próxima execução. O banco continua sendo versionado no repositório pelo workflow, então a memória atravessa execuções distintas do GitHub Actions.

O replay é uma forma de rehearsal supervisionado sobre experiências armazenadas. Ele não transforma o sistema em REINFORCE/PPO nem fornece julgamento semântico geral; a avaliação continua limitada ao avaliador lexical controlado. O runner usa o prefixo de checkpoint v3. Checkpoints anteriores foram treinados com os três pares concatenados e não aprenderam EOS em cada resposta; por isso, não são compatíveis com o novo protocolo e não são restaurados. O workflow restaura apenas artefatos v3 de execuções bem-sucedidas anteriores.

## Limitações

A G2 continua sendo uma arquitetura pequena, byte-level e composta por dois especialistas independentes. A seleção entre continuações não equivale a uma arquitetura de especialistas combinados. Acertar as três perguntas do corpus não demonstra compreensão geral, nem capacidade de responder perguntas inéditas.

## Execução

O workflow não usa cron. Pode ser iniciado manualmente, com seleção das perguntas, número de tentativas e épocas de bootstrap. O limite é de 10 tentativas por execução.
