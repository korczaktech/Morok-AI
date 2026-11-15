# Artemis — G1

Perfil multiagente que utiliza mais de dois agentes.

**Implementado:** execução de três agentes G1 independentes, sementes separadas, alvo CMake `Morok::G1::Artemis` e teste isolado.

A coordenação futura poderá combinar os relatórios dos agentes; a implementação atual executa os três e devolve seus resultados separadamente. Não importa código privado dos outros perfis.
