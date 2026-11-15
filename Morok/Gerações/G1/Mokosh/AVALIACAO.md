# Avaliação — Mokosh

## Estado
Implementação de perfil presente. Esta anotação não afirma que o perfil já passou por uma avaliação individual de respostas.

## Configuração observada
- Geração determinística: temperatura 0.
- Limite de saída: até 128 bytes, limitado também pela política recebida.
- Usa top-k configurável com mínimo 1.
- Penalidade de repetição configurada em 0.15, janela de 24 bytes.
- Usa o motor compartilhado da G1.

## Perguntas de referência
As perguntas oficiais e respostas esperadas estão em ../data/reinforcement/AVALIACAO-PERGUNTAS.md.

## Resultados
- Respostas corretas individuais: ainda não medidas.
- Respostas incorretas individuais: ainda não medidas.
- Tempo de resposta: não registrado; não há milissegundos confiáveis para publicar.
- Comparação de velocidade: pendente de benchmark executado em condições idênticas aos outros perfis.

## Limite dos dados históricos
O banco answers.json contém tentativas do motor compartilhado de reforço da G1, sem campo que identifique Mokosh. Portanto, não é correto atribuir essas respostas ao Mokosh.

## Próxima medição necessária
Executar as três perguntas usando explicitamente o perfil Mokosh, medir o tempo monotônico de cada geração e registrar resposta, veredito, checkpoint, commit e ambiente.
