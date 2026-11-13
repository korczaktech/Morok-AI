# Etapa 4 — Inventário e inicialização dos parâmetros G1

**Objetivo:** implementar a estrutura treinável exata da G1, com 1.000 valores FP32, ordem canônica, inicialização determinística e validação de carga.

## Inventário oficial

| Bloco | Tensores | Parâmetros |
|---|---|---:|
| Estado rápido | W_ax, W_ah, W_cx, W_ch (11×11), b_a, b_c (11) | 506 |
| Estado lento | W_gh, W_gs (11×11), b_g (11) | 253 |
| Cabeça binária | O (8×22), c (8), A triangular estrita (28) | 212 |
| Pontuador de segmentação | v (22), theta_len (7) | 29 |
| **Total** | | **1.000** |

O inventário é verificado por constantes `constexpr` e `static_assert`. A camada matemática não acrescenta parâmetros.

## Representação e precisão

- Todos os parâmetros são armazenados em `float` (IEEE-754 binary32).
- As formas são representadas por `std::array<float, N>` com tamanhos fixos.
- O vetor achatado é uma representação de transferência/carga, não a fonte da semântica dos tensores.
- A contagem inclui todos os pesos e vieses treináveis, inclusive os 29 parâmetros do pontuador de segmentação.
- Não inclui estados recorrentes, gradientes, momentos do otimizador, buffers ou metadados.

## Ordem canônica

A ordem de `flatten()` e `assign()` é exatamente:

1. W_ax (121)
2. W_ah (121)
3. W_cx (121)
4. W_ch (121)
5. b_a (11)
6. b_c (11)
7. W_gh (121)
8. W_gs (121)
9. b_g (11)
10. O (176)
11. c (8)
12. A (28), triangular por linhas conforme especificação da cabeça
13. v (22)
14. theta_len (7)

Matrizes são armazenadas row-major. Os offsets canônicos são 0, 121, 242, 363, 484, 495, 506, 627, 748, 759, 935, 943, 971 e 993; o vetor termina no offset 1000.

## Inicialização

A função `initialize_g1_parameters(parameters, seed)` zera primeiro a estrutura e deriva sementes independentes com SplitMix64. Cada matriz recebe uma semente derivada distinta.

- Xavier uniforme: W_ax, W_cx e O.
- Ortogonal com ganho 1: W_ah, W_ch, W_gh e W_gs.
- b_a e b_c: zero.
- b_g: +1, conforme o baseline operacional G1.
- c, A, v e theta_len: zero.

A rotina reaproveita os inicializadores matemáticos da Etapa 3 e converte explicitamente os resultados para FP32.

A repetibilidade é garantida para a mesma implementação padrão, biblioteca e semente; não se promete sequência idêntica entre bibliotecas C++ distintas.

## Carga e transferência

- `flatten()` retorna os 1.000 parâmetros em ordem canônica.
- `assign(span)` exige exatamente 1.000 valores e rejeita NaN/Inf.
- A validação de tamanho e finitude ocorre antes de qualquer escrita, portanto uma carga inválida não modifica parcialmente o objeto.
- A serialização binária MOROKCKP permanece responsabilidade da etapa de checkpoint; esta etapa fornece a representação canônica de parâmetros.

## Testes de aceitação

- [x] Contagem estática exatamente 1.000.
- [x] Formas e tamanhos fixos de todos os tensores.
- [x] Ordem canônica e offsets de fronteira.
- [x] Inicialização reproduzível para mesma semente.
- [x] Biases conforme baseline G1.
- [x] Todos os valores inicializados finitos.
- [x] Round-trip flatten/assign.
- [x] Rejeição de tamanho incorreto, NaN e infinito sem mutação parcial.
- [ ] CI GCC/Clang Debug/Release aprovado para o commit final.
- [ ] ASan/UBSan executado em ambiente compatível.

## Fora do escopo

Esta etapa não implementa o forward pass, gradientes, otimizador, treinamento, checkpoint ou inferência. Ela entrega o inventário de parâmetros sobre o qual essas partes serão construídas.
