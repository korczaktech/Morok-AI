# Avaliação — Tsukuyomi

## Estado
Implementação de perfil presente. Esta anotação não afirma que o perfil já passou por uma avaliação individual de respostas.

## Configuração observada
- Geração determinística: temperatura 0 e top-k 1.
- Limite de saída: até 256 bytes, limitado também pela política recebida.
- Usa o motor compartilhado da G1.

## Perguntas de referência
As perguntas oficiais e respostas esperadas estão em ../data/reinforcement/AVALIACAO-PERGUNTAS.md.

## Resultados
- Respostas corretas individuais: ainda não medidas.
- Respostas incorretas individuais: ainda não medidas.
- Tempo de resposta: não registrado; não há milissegundos confiáveis para publicar.
- Comparação de velocidade com Apollo e Mokosh: pendente de benchmark executado com o mesmo checkpoint, hardware, prompt e limite de saída.

## Limite dos dados históricos
O banco answers.json contém tentativas do motor compartilhado de reforço da G1, sem campo que identifique Tsukuyomi. Portanto, não é correto atribuir essas respostas ao Tsukuyomi.

## Próxima medição necessária
Executar as três perguntas usando explicitamente o perfil Tsukuyomi, medir o tempo monotônico de cada geração e registrar resposta, veredito, checkpoint, commit e ambiente.
