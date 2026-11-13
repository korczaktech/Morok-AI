# Etapa 7 — Otimizadores e clipping global da G1

**Objetivo:** aplicar os gradientes BPTT da G1 aos 1.000 parâmetros com AdamW como baseline principal e SGD com momentum como baseline comparativo.

## Escopo

- AdamW com learning rate 0,001, beta1 0,9, beta2 0,999, epsilon 1e-8 e weight decay 0,0001.
- SGD com momentum com learning rate 0,01, momentum 0,9 e weight decay 0,0001.
- Clipping global antes da atualização, limite padrão 1,0.
- Estados de otimizador separados e incompatíveis entre si.
- Momentos e velocidade armazenados em FP32, com 1.000 posições por vetor.
- Atualização transacional: parâmetros e estado só são confirmados depois de todos os valores candidatos serem validados.
- Weight decay somente em pesos/matrizes do preditor B0; vieses não são penalizados. Os 29 parâmetros do segmentador ficam completamente fora deste otimizador, pois a segmentação é atualizada em etapa separada.
- Verificação de NaN/Inf, hiperparâmetros, estados e overflow.
- O otimizador não altera os gradientes de entrada. Os slots de estado correspondentes aos 29 parâmetros do segmentador permanecem zerados nesta etapa.

## Ordem canônica e grupos de decay

Usando índices do vetor canônico:

| Intervalo | Tensor | Weight decay |
|---|---|---|
| [0, 484) | W_ax, W_ah, W_cx, W_ch | Sim |
| [484, 506) | b_a, b_c | Não |
| [506, 748) | W_gh, W_gs | Sim |
| [748, 759) | b_g | Não |
| [759, 935) | O | Sim |
| [935, 943) | c | Não |
| [943, 971) | A | Sim |
| [971, 1000) | v, theta_len (segmentador) | Fora do otimizador B0 |

## Algoritmos

### AdamW

Para gradiente já limitado por clipping:

- m_t = beta1*m_(t-1) + (1-beta1)*g_t
- v_t = beta2*v_(t-1) + (1-beta2)*g_t²
- m_hat = m_t / (1-beta1^t)
- v_hat = v_t / (1-beta2^t)
- p <- p*(1-lr*wd) - lr*m_hat/(sqrt(v_hat)+epsilon), somente nos grupos com decay.

O decay é desacoplado dos momentos. Os vieses não recebem decay.

### SGD com momentum

- g_regularizado = g_clipped + wd*p, somente nos grupos com decay.
- velocity_t = momentum*velocity_(t-1) + g_regularizado
- p <- p - lr*velocity_t.

O decay L2 entra no gradiente antes do momentum, conforme a forma clássica de SGD regularizado.

## Contrato da API

- `G1OptimizerConfig::adamw()` e `G1OptimizerConfig::sgd_momentum()` fornecem os padrões aprovados.
- `make_g1_optimizer_state(kind)` cria estado compatível e zerado.
- `apply_g1_optimizer_step(parameters, gradients, config, state)` aplica um passo e retorna contador, norma pré/pós clipping e indicador de clipping.
- Estado AdamW contém primeiro e segundo momentos FP32 (8.000 bytes no total).
- Estado SGD contém velocidade FP32 (4.000 bytes).
- Tentar usar estado de outro otimizador é erro. Não há conversão automática entre estados.
- A função não implementa scheduler, minibatching, treinamento por épocas ou checkpoint.

## Testes

- [x] Padrões de hiperparâmetros.
- [x] Tipos e isolamento dos estados.
- [x] Máscara exata de weight decay para os 1.000 índices.
- [x] AdamW: momentos, bias correction e decay desacoplado.
- [x] SGD: momentum e decay L2.
- [x] Clipping global e norma antes/depois.
- [x] Vieses excluídos do weight decay.
- [x] Rejeição de gradientes/configuração/estado inválidos.
- [x] Atualização transacional em erro e overflow.
- [ ] CI GCC/Clang Debug/Release.
- [ ] ASan/UBSan.

## Limitações

- O otimizador executa um passo sobre um vetor de gradientes já calculado; ainda não há loop de treinamento.
- Não existe scheduler, carregador de corpus, avaliação de validação/teste ou early stopping.
- Não há checkpoint do estado do otimizador nesta etapa.
- O peso decay e os learning rates são valores iniciais de protocolo, não hiperparâmetros empiricamente ótimos.
