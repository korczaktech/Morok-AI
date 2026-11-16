# Morok G3 — orçamento exato de 100.000 parâmetros

## Decisão arquitetural

A configuração G3 foi ajustada para possuir exatamente **100.000 parâmetros treináveis**. O limite é rígido: o backend verifica a contagem no construtor e falha se os tensores alocados não totalizarem exatamente 100.000.

A alteração mantém:
- vocabulário byte-level de 261 IDs;
- duas camadas GRU;
- RMSNorm entre as camadas;
- projeção da última camada para o espaço de embedding;
- weight tying real, usando o mesmo armazenamento para embedding e matriz de saída;
- inicialização Xavier/Glorot e recorrência ortogonal por porta;
- contagem automática a partir dos tamanhos reais dos tensores.

As dimensões de referência foram ajustadas. A especificação arquitetural define os valores iniciais como configuráveis; portanto, esta revisão troca a configuração E=64/H=128 por dimensões que fecham o orçamento sem adicionar parâmetros artificiais ou esconder pesos do contador.

## Dimensões normativas da configuração 100K

| Elemento | Dimensão |
|---|---:|
| Vocabulário | 261 |
| Embedding E | 60 |
| Hidden da GRU 1 (H1) | 97 |
| Hidden da GRU 2 (H2) | 68 |
| Camadas recorrentes | 2 |
| Portas por GRU | 3 |
| RMSNorm | escala aprendível de 97 |
| Projeção de saída | 68 → 60 |
| Matriz de saída | embedding transposto, compartilhado |

As camadas têm dimensões ocultas diferentes. Isso é intencional e permite manter a GRU completa, com matrizes densas e biases explícitos, sem inserir vetores de preenchimento ou parâmetros fictícios para alcançar o total.

## Contagem verificável

| Tensor | Cálculo | Parâmetros |
|---|---:|---:|
| Embedding | 261 × 60 | 15.660 |
| GRU 1 — entrada | 3 × 97 × 60 | 17.460 |
| GRU 1 — recorrência | 3 × 97 × 97 | 28.227 |
| GRU 1 — bias | 3 × 97 | 291 |
| GRU 2 — entrada | 3 × 68 × 97 | 19.788 |
| GRU 2 — recorrência | 3 × 68 × 68 | 13.872 |
| GRU 2 — bias | 3 × 68 | 204 |
| RMSNorm — escala | 97 | 97 |
| Projeção | 60 × 68 | 4.080 |
| Bias da projeção | 60 | 60 |
| Bias de saída | 261 | 261 |
| **Total** | | **100.000** |

A matriz de saída não aparece como uma segunda linha porque não existe uma segunda matriz de pesos: a projeção usa a transposta do tensor embedding já contabilizado. Isso é weight tying por armazenamento único.

## Estado recorrente

O estado por sessão contém os dois vetores ocultos:
- camada 1: 97 valores FP32;
- camada 2: 68 valores FP32.

Total: 165 valores FP32 = **660 bytes** por estado, sem contar metadados e alinhamento.

## Identificação e compatibilidade

O identificador de arquitetura foi alterado de `0x0301` para `0x0302`. Artefatos/checkpoints da configuração anterior não devem ser carregados como se fossem compatíveis. O carregador precisa comparar o identificador e as dimensões antes de aceitar pesos.

## Garantias no código

- `count_parameters` soma os tamanhos reais dos vetores de pesos.
- `G3Backend::parameter_count()` expõe essa contagem calculada.
- `descriptor().parameter_count` reporta a contagem calculada.
- O construtor exige `parameter_count() == 100000`; caso contrário, lança erro.
- Os testes conferem cada formato de tensor, o total exato, a contagem do descritor, a inicialização, a ortogonalidade, o forward, o weight tying e o isolamento de sessão.

A contagem de 100.000 refere-se a parâmetros treináveis do modelo. Estados temporários de execução, buffers de sessão, gradientes e estados do otimizador não são parâmetros do modelo e não entram nesse número.
