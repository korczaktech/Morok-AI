# G2

A segunda geração organiza três perfis de modelo, com orçamentos definidos:

| Modelo | Perfil | Parâmetros |
|---|---|---:|
| **Tsukuyomi** | Mais avançado | 10.000 |
| **Apollo** | Comum / uso geral | 5.000 |
| **Mokosh** | Rápido | 2.000 |
| **Total** | | **17.000** |

- **Tsukuyomi:** prioriza capacidade e tarefas complexas.
- **Apollo:** busca equilíbrio para uso geral.
- **Mokosh:** prioriza velocidade, baixa latência e menor custo computacional.

## Estado de implementação

- **Tsukuyomi G2 v0.1.0:** modelo byte-level executável, com 10.000 parâmetros, treinamento Adam, avaliação de loss, geração autoregressiva greedy, checkpoint versionado, CLIs e teste CTest próprio.
- **Apollo:** perfil documentado; implementação pendente.
- **Mokosh:** perfil documentado; implementação pendente.

A versão 0.1 do Tsukuyomi estabelece o primeiro pipeline da G2. Ela não comprova qualidade linguística nem raciocínio geral. Arquitetura e limitações estão descritas em Tsukuyomi/ARCHITECTURE.md.
