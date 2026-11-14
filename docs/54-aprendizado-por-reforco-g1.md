# Experimento de aprendizado por reforço da G1

## Objetivo

Treinar a G1 para responder a exatamente três perguntas controladas:

1. "O céu é azul?" — conceito esperado: sim ou azul.
2. "As montanhas são altas ou baixas?" — conceito esperado: altas.
3. "Que cor é a grama?" — conceito esperado: verde.

A avaliação é deliberadamente estreita. Acertar essas três perguntas não significa que a G1 adquiriu conhecimento geral ou compreensão ampla da linguagem.

## Database de feedback

O arquivo versionado `data/reinforcement/answers.json` é a base de dados do experimento. Cada episódio registra:

- pergunta e prompt utilizados;
- resposta integral produzida pela G1;
- classificação `correct` ou `incorrect`;
- recompensa numérica (+1 ou -1);
- justificativa do avaliador;
- horário UTC e hashes do checkpoint antes/depois;
- resultado da atualização de parâmetros.

O histórico é append-only pelo runner: novas respostas são acrescentadas, sem sobrescrever episódios anteriores. O workflow grava as alterações diretamente na branch `main`.

## Ciclo de reforço

O workflow `reinforcement` executa a cada 15 minutos ou manualmente:

1. restaura o último checkpoint publicado como artifact;
2. se não existir checkpoint, inicializa a G1 com um bootstrap supervisionado curto usando o corpus factual existente;
3. gera uma resposta amostrada para cada uma das três perguntas;
4. aplica um avaliador lexical restrito às palavras/conceitos esperados e à negação;
5. registra a resposta e a recompensa na database JSON;
6. aplica uma atualização reward-weighted à G1;
7. publica o checkpoint atualizado como artifact e comita o histórico JSON na `main`.

A atualização de parâmetros usa o gradiente BPTT da sequência gerada como aproximação do score-function/REINFORCE: recompensa positiva aumenta a probabilidade aproximada da sequência; recompensa negativa a reduz. A atualização é limitada por clipping, taxa de aprendizado pequena e validação de finitude. O modelo permanece com exatamente 1.000 parâmetros treináveis.

## Limitações e interpretação

- O avaliador é lexical, não um juiz semântico geral. Por exemplo, uma frase longa pode conter a palavra esperada sem responder corretamente; negações simples são rejeitadas.
- A atualização usa o gradiente da perda byte-level como surrogate da política amostrada; ela não é uma implementação exata da distribuição renormalizada sobre bytes UTF-8 válidos.
- Recompensas negativas podem apenas reduzir a probabilidade de uma resposta errada; sem exploração que encontre respostas corretas, não há sinal positivo para ensinar a resposta desejada.
- O bootstrap supervisionado inicial não é aprendizado por reforço; ele apenas fornece um ponto de partida. Os episódios seguintes usam recompensas.
- Checkpoints são preservados como artifacts do Actions por 90 dias; a database JSON fica versionada no repositório.
- A frequência do cron do GitHub Actions não garante execução exata a cada 15 minutos.

## Critério de progresso

O relatório deve ser lido por pergunta, não apenas pela taxa total. A evidência mínima de progresso é observar respostas corretas repetidas, com variações de amostragem, em episódios posteriores. O experimento não deve marcar sucesso sem verificar as respostas geradas e o histórico de recompensas.
