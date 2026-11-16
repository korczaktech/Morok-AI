# Mokosh — G3

Perfil de baixo consumo. Usa o backend compartilhado de 100.000 parâmetros com temperatura 0,55, Top-K 4, entrada limitada a 2.048 bytes, saída limitada a 192 bytes e capacidade recomendada de oito sessões. A redução de custo vem dos limites por requisição e da concorrência, não de pesos menores.

Implementação executável: `morok::g3::profiles::ProfileId::mokosh`. Detalhes: [Perfis G3](../profiles/README.md).
