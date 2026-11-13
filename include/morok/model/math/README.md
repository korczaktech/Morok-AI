# Primitivas matemáticas

A camada `morok::math` é independente do modelo e não possui parâmetros treináveis próprios. Ela usa `double` para acumulação e testes numéricos; o formato de parâmetros FP32 do checkpoint G1 continua sendo uma decisão separada.

## Operações

- `Matrix`: matriz row-major com dimensões verificadas.
- `matvec`: multiplicação matriz-vetor com validação dimensional.
- `sigmoid`: implementação por ramos para evitar overflow em logits extremos.
- `tanh`: função padrão da biblioteca matemática.
- `softplus` e `binary_cross_entropy_with_logits`: formas estáveis para valores extremos.
- `log_sum_exp` e `softmax`: normalização pelo máximo.
- `global_norm` e `clip_global_norm`: norma global com acumulação via `hypot`; clipping in-place.
- `initialize_xavier_uniform`: Xavier/Glorot uniforme com semente explícita.
- `initialize_orthogonal`: base ortogonal via Gram-Schmidt modificado, adequada às matrizes retangulares pequenas da G1.

## Regras

- Sementes iguais e mesma implementação padrão devem reproduzir a mesma sequência.
- Inicialização é determinística por chamada e não compartilha estado global.
- Softmax rejeita logits não finitos; BCE valida o alvo.
- Gradientes não finitos são rejeitados antes do clipping.
- O clipping retorna a norma original e altera apenas os gradientes quando necessário.
- As rotinas não alteram o inventário dos 1.000 parâmetros treináveis.
- Gram-Schmidt é apropriado para dimensões pequenas da G1; não é promessa de melhor condicionamento para matrizes enormes.

A validação de precisão e estabilidade está em `tests/unit/model/test_math_primitives.cpp`.
