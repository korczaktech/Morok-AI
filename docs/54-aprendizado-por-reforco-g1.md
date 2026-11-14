# Experimento de aprendizado por reforço da G1

## Objetivo

Treinar a G1 para responder a exatamente três perguntas controladas, usando os enunciados curtos originais como contexto:

1. "O céu é azul?" — conceito esperado: sim ou azul.
2. "As montanhas são altas ou baixas?" — conceito esperado: altas.
3. "Que cor é a grama?" — conceito esperado: verde.

A avaliação é deliberadamente estreita. Acertar essas três perguntas não significa que a G1 adquiriu conhecimento geral ou compreensão ampla da linguagem.

## Execução manual com quantidade definida

Ao iniciar o workflow `reinforcement` pelo botão **Run workflow**, o formulário apresenta o campo numérico `attempts` (1 a 100, validado pelo runner). O valor representa o total de respostas geradas naquela execução, não o total por pergunta. As três perguntas são escolhidas em rodízio; por exemplo, 8 tentativas percorrem céu, montanhas, grama, céu, montanhas, grama, céu e montanhas.

O laço executa exatamente a quantidade solicitada e não encerra antecipadamente por acertos ou erros. Cada resposta é classificada, recebe recompensa +1/-1, é registrada na database JSON e aciona a atualização de política quando há bytes suficientes para o BPTT. Uma falha técnica de execução ainda interrompe o workflow, em vez de fingir que as tentativas restantes foram concluídas.

No final, a etapa **Display attempt results** apresenta somente a lista numerada no formato `1. resposta - correto` ou `2. resposta - errado`. O resumo da execução também mostra a tabela com pergunta, resposta e veredito. O histórico completo permanece em `data/reinforcement/answers.json`.

## Database de feedback

O arquivo versionado `data/reinforcement/answers.json` é a base de dados do experimento. Cada episódio registra:

- pergunta e prompt utilizados;
- resposta integral produzida pela G1;
- classificação `correct` ou `incorrect`;
- recompensa numérica (+1 ou -1);
- justificativa do avaliador;
- horário UTC e hashes do checkpoint antes/depois;
- resultado da atualização de parâmetros.

O histórico é append-only pelo runner: demonstrações-oráculo e respostas geradas são acrescentadas, sem sobrescrever episódios anteriores. Cada episódio recebe um identificador estável baseado na execução do GitHub Actions para evitar colisões quando ciclos são enfileirados. O campo \`source\` distingue demonstrações de respostas amostradas. O workflow grava as alterações diretamente na branch `main`.

## Ciclo de reforço

Na primeira execução, três demonstrações-oráculo curtas (\`Sim.\`, \`Altas.\`, \`Verde.\`) recebem recompensa positiva e são identificadas como \`oracle_demonstration\` no JSON. Esse aquecimento é supervisionado/reward-seeded, não uma descoberta autônoma; ele evita começar com apenas recompensas negativas quando a política ainda produz bytes sem sentido. Depois, as respostas geradas pelo próprio modelo são avaliadas e recebem recompensas automaticamente.

O workflow `reinforcement` executa a cada 15 minutos ou manualmente:

1. restaura o último checkpoint publicado como artifact;
2. se não existir checkpoint, inicializa a G1 com um bootstrap supervisionado curto usando o corpus factual existente;
3. gera uma resposta amostrada para cada uma das três perguntas;
4. aplica um avaliador lexical restrito às palavras/conceitos esperados e à negação;
5. registra a resposta e a recompensa na database JSON;
6. aplica uma atualização reward-weighted à G1;
7. publica o checkpoint atualizado como artifact e comita o histórico JSON na `main`.

A atualização de parâmetros usa o gradiente BPTT da sequência como aproximação do score-function/REINFORCE: recompensa positiva aumenta a probabilidade aproximada da sequência; recompensa negativa a reduz. O gerador do experimento usa modo somente imprimível e seleção gulosa (top-k 1) para impedir que bytes de controle ASCII e amostragem excessivamente aleatória contaminem as respostas. A atualização é limitada por clipping, taxa de aprendizado pequena e validação de finitude. O modelo permanece com exatamente 1.000 parâmetros treináveis.

## Limitações e interpretação

- O avaliador é lexical, não um juiz semântico geral. Por exemplo, uma frase longa pode conter a palavra esperada sem responder corretamente; negações simples são rejeitadas.
- A atualização usa o gradiente da perda byte-level como surrogate da política amostrada; ela não é uma implementação exata da distribuição renormalizada sobre bytes UTF-8 válidos.
- Recompensas negativas podem apenas reduzir a probabilidade de uma resposta errada; sem exploração que encontre respostas corretas, não há sinal positivo para ensinar a resposta desejada.
- O bootstrap supervisionado inicial não é aprendizado por reforço; ele apenas fornece um ponto de partida. Os episódios seguintes usam recompensas.
- Checkpoints são preservados como artifacts do Actions por 90 dias; a database JSON fica versionada no repositório.
- A frequência do cron do GitHub Actions não garante execução exata a cada 15 minutos.

## Critério de progresso

O relatório deve ser lido por pergunta, não apenas pela taxa total. A evidência mínima de progresso é observar respostas corretas repetidas, com variações de amostragem, em episódios posteriores. O experimento não deve marcar sucesso sem verificar as respostas geradas e o histórico de recompensas.
