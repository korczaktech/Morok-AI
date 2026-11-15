# 4. Modelo e escala de parâmetros

## 4.1 Regra de contagem

A contagem inclui todos os valores treináveis da arquitetura, não apenas a matriz principal. Devem ser contabilizados:
- parâmetros de entrada e projeção;
- parâmetros do núcleo recorrente;
- parâmetros de saída;
- parâmetros treináveis de segmentação;
- parâmetros da representação hierárquica;
- parâmetros de controle contextual;
- quaisquer vieses, escalas ou vetores treináveis.

Estados transitórios, buffers, contadores, tabelas fixas e dados persistidos não são parâmetros treináveis. A classificação deverá ser explícita no inventário de cada geração.

## 4.2 Progressão

A progressão aprovada segue a sequência baseada em 1, 2, 5 e 10:

| Etapa | Parâmetros treináveis |
|---|---:|
| G1 | 1.000 |
| G2 | 2.000 |
| G3 | 5.000 |
| G4 | 10.000 |
| G5 | 20.000 |
| G6 | 50.000 |
| G7 | 100.000 |
| G8 | 200.000 |
| G9 | 500.000 |
| G10 | 1.000.000 |
| G11 | 2.000.000 |
| G12 | 5.000.000 |
| G13 | 10.000.000 |

A progressão poderá continuar no mesmo padrão, desde que a etapa anterior tenha sido avaliada.

## 4.3 Precisão numérica inicial

A primeira implementação experimental utilizará FP32 para facilitar depuração e validação numérica. Precisões menores, como FP16 ou INT8, serão avaliadas posteriormente e somente adotadas quando houver suporte, estabilidade e benefício medido.

## 4.4 Crescimento não automático

Aumentar parâmetros não é, por si só, uma otimização. Cada geração deverá apresentar:
- melhora em métricas de validação;
- custo de inferência;
- custo de treinamento;
- pico de RAM;
- tamanho de checkpoint;
- comparação com uma referência de orçamento equivalente.

## 4.5 Independência entre gerações

O formato de parâmetros deverá incluir versão de arquitetura e metadados. Conversões entre gerações só serão permitidas quando houver procedimento explícito e testado. Não se presumirá compatibilidade entre dimensões diferentes.
