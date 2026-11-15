# Reinforcement-G2

Este workflow aplica reforço exclusivamente à G2. A G2 possui dois especialistas independentes de 1.000 parâmetros; cada tentativa calcula gradientes BPTT e aplica a recompensa aos dois especialistas, totalizando 2.000 parâmetros treináveis.

## Isolamento

- Checkpoint G2 usa o formato pareado `.expert-a` e `.expert-b`.
- O histórico fica em `data/reinforcement-g2/answers.json`, sem compartilhar episódios com G1.
- O artefato do Actions tem nome `morok-g2-reinforcement-state`.
- O workflow é independente: `.github/workflows/reinforcement-g2.yml`.
- A geração usa `morok-generate-g2`; o reforço usa `morok-reinforce-g2`.

## Limitações

A recompensa agora respeita o sentido do otimizador: recompensa positiva aplica descida do gradiente da perda da sequência observada; recompensa negativa inverte o gradiente para desencorajar essa sequência. Isso é uma aproximação reward-weighted BPTT, não um estimador REINFORCE completo com baseline. O avaliador lexical é restrito às três perguntas controladas e não mede compreensão geral. Checkpoints preservam os pesos, mas o estado do otimizador é reinicializado em cada processo de reforço. Resultados de treinamento não devem ser interpretados como prova de competência linguística.

## Seleção de perguntas

Na execução manual de `Reinforcement-G2`, os três campos booleanos aparecem como caixas de seleção: pergunta sobre o céu, sobre as montanhas e sobre a cor da grama. É necessário selecionar pelo menos uma. Em execuções agendadas, as três perguntas são selecionadas automaticamente. O número de tentativas é distribuído apenas entre as perguntas selecionadas.

O corpus de bootstrap continua contendo exatamente um exemplo: `Usuario: O ceu e azul?\nIA: Sim, o ceu e azul`. O prompt da pergunta sobre o céu foi alinhado ao prefixo literal desse exemplo. As perguntas sobre montanhas e grama são opções de avaliação, mas não possuem exemplos correspondentes no corpus atual; selecioná-las não significa que a G2 já tenha aprendido essas respostas.

## Diagnóstico da repetição de caracteres

O histórico anterior registrou 212 episódios incorretos e zero corretos. A saída recorrente de caracteres `e` confirma que o modelo não aprendeu a responder às perguntas. O reforço anterior apenas aplicava recompensas negativas às sequências geradas; isso não fornece ao modelo a resposta correta. O artefato persistido foi versionado para `morok-g2-reinforcement-state-v2`, de modo que a próxima execução faça bootstrap de um checkpoint novo em vez de continuar a trajetória degradada anterior. O bootstrap manual padrão foi elevado para 100 épocas por especialista.

Isso corrige a reutilização do checkpoint antigo e o desalinhamento do prompt do céu, mas não prova que o modelo conseguirá gerar uma resposta correta. O corpus de um único exemplo é insuficiente para ensinar as outras perguntas ou linguagem natural geral. A qualidade deverá ser confirmada pelos resultados reais da próxima execução.
