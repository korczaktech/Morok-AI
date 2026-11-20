# Dataset independente do RL G3

Arquivo: `g3_rl_ptbr_v1.jsonl`

Este dataset é um snapshot próprio para experimentos de REINFORCE. Ele contém
somente registros não sensíveis com `quality=1.0` do corpus pt-BR revisado da
G3. Os 104 registros marcados com `quality=0.80` e pendentes de revisão humana
não são copiados para este conjunto.

O snapshot mantém a proveniência original em `source` e usa a versão
`g3-rl-ptbr-v1`. Ele é um conjunto separado, com fingerprint próprio, para
que alterações no corpus SFT não mudem silenciosamente os dados de um treino
RL já definido. Não é um modelo SFT, não contém pesos nem depende de checkpoint
SFT.

A rubrica RL usa as respostas deste arquivo como referência lexical. Portanto,
a recompensa é experimental e não equivale a avaliação semântica ou factual.
A ampliação do corpus deve preservar proveniência, licença, revisão humana e
separação por `group_id`.
