# Etapa 3 — Primitivas matemáticas e estabilidade numérica

**Objetivo:** estabelecer uma camada matemática pequena, determinística e testável para a implementação do modelo G1, sem introduzir framework de machine learning nem alterar a contagem de parâmetros treináveis.

## Escopo implementado

- Matriz row-major com dimensões explícitas, acesso verificado e armazenamento contíguo.
- Multiplicação matriz-vetor com validação dimensional.
- Sigmoid estável por ramos, evitando calcular `exp(-x)` para entradas negativas extremas.
- Tanh via biblioteca matemática padrão.
- Softplus estável: `max(x, 0) + log1p(exp(-abs(x)))`.
- Binary cross-entropy com logits, sem sigmoid separado e sem logaritmos de probabilidades arredondadas.
- Log-sum-exp com deslocamento pelo máximo.
- Softmax estável e rejeição de logits não finitos.
- Norma global de gradientes usando `hypot` e clipping in-place.
- Inicialização Xavier/Glorot uniforme e ortogonal determinística por semente explícita.
- Testes de extremos numéricos, normalização, dimensões, clipping, determinismo e ortogonalidade em matrizes quadradas e retangulares.

## Precisão

A API matemática usa `double` para acumulação e testes. Isso não altera a decisão de armazenar parâmetros do checkpoint G1 em FP32 IEEE-754 binary32. Conversões para FP32 devem ocorrer em fronteiras explícitas da implementação do modelo/serialização, e não implicitamente dentro dessas primitivas.

## Inicialização ortogonal

A implementação usa Gram-Schmidt modificado, adequado às matrizes pequenas de dimensão 11×11 da G1. Para matrizes altas, as colunas são ortonormais; para matrizes largas, as linhas são ortonormais. A rotina valida o ganho e falha explicitamente caso a base se torne degenerada.

## Determinismo

Cada inicializador recebe uma semente explícita e instancia seu próprio gerador `std::mt19937_64`; não há estado aleatório global. A repetibilidade é garantida para a mesma implementação padrão, compilador/biblioteca e semente. Não se promete sequência idêntica entre todas as bibliotecas padrão C++.

## Limites e exclusões

- Nenhum parâmetro treinável é criado por esta camada.
- Não há dependência de BLAS, CUDA, GPU ou framework ML.
- Não há autodiferenciação; os gradientes serão implementados manualmente na etapa de treinamento.
- A rotina ortogonal é voltada à escala pequena da G1, não a matrizes grandes.
- A presença dos testes não substitui medição de desempenho no hardware-alvo.

## Critérios de aceite

- [x] API matemática independente do modelo.
- [x] Ativações e perdas estáveis para logits extremos.
- [x] Softmax normalizado e estável.
- [x] Operação matriz-vetor e validação de dimensões.
- [x] Norma e clipping global de gradientes.
- [x] Inicializações determinísticas Xavier e ortogonal.
- [x] Testes automatizados cobrindo os casos acima.
- [ ] CI GCC/Clang em Debug e Release aprovado para o commit desta etapa.
- [ ] ASan/UBSan executado em ambiente compatível.

Os dois últimos itens dependem da execução real dos workflows; não são marcados como aprovados apenas porque os arquivos foram adicionados.
