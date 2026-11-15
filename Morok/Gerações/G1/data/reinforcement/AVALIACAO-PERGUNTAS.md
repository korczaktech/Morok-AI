# Avaliação de perguntas — G1

## Perguntas oficiais e gabarito

| ID | Pergunta | Resposta de referência | Critério automático atual |
|---|---|---|---|
| sky_is_blue | O céu é azul? | Sim. | Presença de “sim” ou “azul”, sem negação |
| grass_color | Que cor é a grama? | Verde. | Presença de “verde”, sem negação |
| mountains_high_or_low | As montanhas são baixas ou altas? | Altas. | Presença de “altas”, sem negação |

O critério é lexical e limitado a este experimento; não é um avaliador semântico geral.

## Auditoria do histórico existente

Fonte: answers.json, schema_version 1, consultado no commit atual.

- Episódios registrados: 169.
- Demonstrações-oráculo: 21.
- Tentativas geradas pelo modelo: 148.
- Vereditos corretos: 21 (todos são demonstrações-oráculo).
- Vereditos incorretos: 148.
- Atualizações de política registradas: 169.
- Tentativas com duração/latência registrada: 0.

Por pergunta, o histórico contém:
- Céu azul: 7 demonstrações corretas e 50 respostas geradas incorretas.
- Cor da grama: 7 demonstrações corretas e 49 respostas geradas incorretas.
- Altura das montanhas: 7 demonstrações corretas e 49 respostas geradas incorretas.

As demonstrações-oráculo são respostas de referência fornecidas para inicialização do treinamento; não devem ser contadas como acertos espontâneos do modelo.

## Por que não há resultados por perfil

O histórico atual não possui identificador de perfil (Tsukuyomi, Mokosh ou Apollo) nem duração de inferência. Ele registra uma política/checkpoint compartilhado da G1. Assim, os resultados acima não podem ser distribuídos entre os três perfis.

O workflow G1 atual executa compilação, instalação, testes CTest e smoke test de primeiro treinamento/retomada. Ele não executa a bateria de três perguntas por perfil nem publica um relatório de latência por perfil. Por isso, os resultados não aparecem como etapas específicas do GitHub Actions.

## Estado da avaliação individual

| Perfil | Correção por pergunta | Tempo por pergunta |
|---|---|---|
| Tsukuyomi | Não medido isoladamente | Não medido |
| Mokosh | Não medido isoladamente | Não medido |
| Apollo | Não medido isoladamente | Não medido |

Não preencher tempos estimados. Uma avaliação válida deve usar o mesmo checkpoint inicial, as mesmas perguntas e condições de hardware, executar cada perfil separadamente, medir com relógio monotônico e salvar resposta, duração em milissegundos e veredito em artefato do Actions.

## Interpretação

O mecanismo de atualização por recompensa está sendo executado e registra mudanças nos parâmetros, mas isso não significa que a capacidade de responder corretamente esteja funcionando. No histórico atual, as 148 respostas geradas foram classificadas como incorretas. Portanto, o reforço está operacional como atualização de parâmetros, porém o experimento ainda não demonstra aprendizagem eficaz nas três tarefas.
