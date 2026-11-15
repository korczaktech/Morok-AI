# Avaliação — Apollo

## Estado
Implementação de perfil presente. Esta anotação não afirma que o perfil já passou por uma avaliação individual de respostas.

## Configuração observada
- Geração determinística: temperatura 0 e top-k 1.
- Limite de saída: até 512 bytes, limitado também pela política recebida.
- Sem penalidade de repetição.
- Usa o motor compartilhado da G1.

## Perguntas de referência
As perguntas oficiais e respostas esperadas estão em ../data/reinforcement/AVALIACAO-PERGUNTAS.md.

## Resultados
- Respostas corretas individuais: ainda não medidas.
- Respostas incorretas individuais: ainda não medidas.
- Tempo de resposta: não registrado; não há milissegundos confiáveis para publicar.
- Meta de velocidade de 30% acima do Tsukuyomi: ainda não comprovada por benchmark comparativo.

## Limite dos dados históricos
O banco answers.json contém tentativas do motor compartilhado de reforço da G1, sem campo que identifique Apollo. Portanto, não é correto atribuir essas respostas ao Apollo.

## Próxima medição necessária
Executar as três perguntas usando explicitamente o perfil Apollo, medir o tempo monotônico de cada geração e registrar resposta, veredito, checkpoint, commit e ambiente.
