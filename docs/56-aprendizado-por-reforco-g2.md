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

O bootstrap usa um único documento em `tests/fixtures/g2-single-document/doc-01.txt`, com três exemplos controlados:

- `Usuario: O ceu e azul?` → `Sim, o ceu e azul`.
- `Usuario: As montanhas sao altas ou baixas?` → `As montanhas sao altas.`.
- `Usuario: Que cor e a grama?` → `A grama e verde.`.

Como há um único documento, a avaliação do bootstrap ocorre no próprio documento de treinamento. Ela não é validação independente e não mede generalização.

## Correções de aprendizado

O otimizador minimiza a perda subtraindo o gradiente. Portanto, recompensa positiva deve aplicar o gradiente BPTT sem inverter seu sinal; recompensa negativa deve inverter o sinal. O atualizador G2 agora segue essa convenção.

O teste unitário compara a perda da sequência antes e depois da atualização para os dois especialistas. Ele exige que recompensa positiva reduza a perda e que o gradiente invertido aumente a perda. Isso valida a direção da atualização, não apenas a alteração dos parâmetros.

Quando a resposta gerada está errada ou degenerada, o runner usa a resposta de referência como demonstração-oráculo. Essa correção é treinamento supervisionado guiado por referência, não aprendizado autônomo nem uma implementação completa de REINFORCE.

## Geração e avaliação

O executável de geração G2 ativa saída imprimível, penalização de repetição e um limite de bytes. Essas medidas reduzem saídas degeneradas, mas não garantem uma resposta semanticamente correta.

O avaliador exige os conceitos esperados em cada resposta:
- Céu: `sim` e `azul`.
- Montanhas: `montanhas` e `altas`.
- Grama: `grama` e `verde`.

Respostas incompletas, negativas ou degeneradas não recebem recompensa positiva.

## Persistência

O runner não apaga mais os checkpoints de inferência a cada execução. Se os dois checkpoints de especialistas já existirem, o treinamento continua a partir desses pesos. Se ambos não existirem, o runner faz o bootstrap; se existir apenas um, interrompe com erro para evitar um par inconsistente.

Os checkpoints de treinamento do bootstrap também podem ser retomados com `--resume` quando ambos estiverem disponíveis. O banco de episódios permanece separado dos pesos do modelo. Checkpoints produzidos por versões anteriores à correção do sinal do gradiente devem ser descartados uma única vez antes de executar a versão corrigida; caso contrário, o runner continuará a partir dos pesos antigos.

## Limitações

A G2 continua sendo uma arquitetura pequena, byte-level e composta por dois especialistas independentes. A seleção entre continuações não equivale a uma arquitetura de especialistas combinados. Acertar as três perguntas do corpus não demonstra compreensão geral, nem capacidade de responder perguntas inéditas.

## Execução

O workflow não usa cron. Pode ser iniciado manualmente, com seleção das perguntas, número de tentativas e épocas de bootstrap. O limite é de 10 tentativas por execução.
