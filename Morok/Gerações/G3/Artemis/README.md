# Artemis — G3

Perfil multiagente com três agentes independentes por requisição. Cada agente usa o mesmo backend G3 de 100.000 parâmetros, com sessão recorrente e RNG isolados. A API retorna as três respostas separadamente; não há consolidação automática.

Implementação executável: `morok::g3::profiles::ProfileId::artemis`. Detalhes: [Perfis G3](../profiles/README.md).
