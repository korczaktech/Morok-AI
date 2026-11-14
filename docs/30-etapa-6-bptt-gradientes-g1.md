# Etapa 8 operacional — BPTT e gradientes manuais da G1

> Este arquivo manteve o nome histórico `30-etapa-6` da organização documental anterior. Na sequência operacional de 20 etapas aprovada posteriormente, este escopo corresponde à **etapa 8: Gradientes e BPTT**. A etapa 7 é dedicada aos otimizadores.

**Objetivo:** calcular gradientes do preditor B0 da G1 por reverse-mode manual, retropropagando através dos estados fast/slow dentro de janelas truncadas.

## Escopo implementado

- Gradiente da cabeça binária autoregressiva: matriz O, viés c e matriz triangular A.
- Gradiente do estado rápido: W_ax, W_ah, W_cx, W_ch, b_a e b_c.
- Gradiente do estado lento: W_gh, W_gs e b_g.
- BPTT em ordem temporal inversa dentro de cada bloco.
- Estado numérico fast/slow transportado entre blocos.
- Grafo de gradientes interrompido na fronteira de cada bloco.
- Perda e gradientes apresentados como média por byte-alvo.
- Acumulação em double; parâmetros e estados continuam FP32.
- Os 29 parâmetros do segmentador permanecem com gradiente zero, pois não participam do preditor B0.
- Verificação de finitude dos parâmetros e do estado inicial antes do cálculo.
- Verificação de finitude da perda e de todos os gradientes ao final.
- Cálculo seguro do fim da janela mesmo quando o chamador fornece truncation_length próximo de SIZE_MAX.

## Convenção temporal

Para uma sequência de bytes b0,...,bN-1, o modelo consome b(i-1), atualiza fast/slow e prevê bi, para i=1,...,N-1. Portanto há exatamente N-1 bytes-alvo. O estado inicial representa o estado imediatamente anterior ao consumo de b0; pode ser zero ou um estado finito transportado pelo chamador.

Cada bloco contém no máximo B bytes-alvo. Ao mudar de bloco, o estado numérico final é preservado, mas o gradiente do estado é zerado: não existe caminho de derivada entre blocos. Isso implementa truncamento real, sem reinicializar o contexto forward.

O estado retornado fica após consumir o byte de contexto do último alvo, isto é, imediatamente antes do consumo do último byte-alvo da sequência. O chamador deve reutilizar esse estado e sobrepor o último byte-alvo como primeiro byte de contexto da janela seguinte. Essa convenção evita repetir a perda do byte sobreposto.

## Derivadas

Na cabeça, para cada bit j, delta_j = sigmoid(logit_j)-target_j. Os bits anteriores do byte-alvo são prefixos observados constantes. Seus gradientes não são propagados. São acumulados gradientes em O, c e A, e o gradiente do estado combinado é calculado por O transposta vezes delta.

No estado lento, s'=g*s_prev+(1-g)*h:
- delta_g = delta_s'*(s_prev-h)
- delta_zg = delta_g*g*(1-g)
- delta_s_prev = delta_s'*g + W_gs transposta * delta_zg
- delta_h = delta_h_direto + delta_s'*(1-g) + W_gh transposta * delta_zg

No estado rápido, h'=a*h_prev+(1-a)*candidate:
- delta_a = delta_h'*(h_prev-candidate)
- delta_za = delta_a*a*(1-a)
- delta_zc = delta_h'*(1-a)*(1-candidate²)

Os caminhos diretos e recorrentes são somados antes de propagar para o estado anterior. Gradientes de matrizes são produtos externos; gradientes de vieses são somas temporais.

A derivada atravessa o cast FP32 como straight-through identity. Isso é uma aproximação deliberada para a operação de armazenamento; diferenças finitas em parâmetros FP32 usam tolerância numérica.

## API

- `G1Gradients`: vetor double com 1.000 posições na ordem canônica do inventário; inclui norma global.
- `G1BpttResult`: perda média em nats por byte-alvo, quantidade de alvos, gradientes e estado final.
- `compute_g1_bptt_gradients(parameters, bytes, truncation_length, initial_state)`: calcula forward e backward de uma sequência. Exige pelo menos dois bytes, truncation_length positivo, parâmetros finitos e estado inicial finito.

A função não altera parâmetros nem estado externo. Não aplica clipping, atualizador, weight decay ou scheduler.

## Testes

- [x] Contagem de alvos igual a N-1.
- [x] Perda e todos os gradientes finitos.
- [x] Gradientes dos 29 parâmetros do segmentador iguais a zero.
- [x] Perda forward e estado final invariantes aos tamanhos de janela 1, 2 e sequência inteira.
- [x] Janela SIZE_MAX sem overflow aritmético.
- [x] Comparação independente da perda com o caminho de produção `advance_g1_state` + `evaluate_g1_next_byte`.
- [x] Diferenças finitas centrais cobrindo um parâmetro de cada tensor treinável.
- [x] Diferenças finitas com estado inicial fast/slow não nulo.
- [x] Verificação de que o truncamento corta dependências temporais dos gradientes, sem alterar estado forward ou perda.
- [x] Rejeição de sequência curta e truncation_length zero.
- [x] Rejeição de NaN/Inf no estado inicial e de parâmetros não finitos.
- [ ] CI GCC/Clang Debug/Release aprovado no commit final desta revisão.
- [ ] ASan/UBSan aprovado no commit final desta revisão.

## Fora do escopo

AdamW, SGD momentum e clipping aplicado ao treinamento pertencem à etapa 7 operacional. Batching, carregamento de corpus, treinamento por épocas, checkpoint, scheduler e avaliação BPB em corpus pertencem às etapas posteriores. O segmentador treinável também não faz parte do preditor B0 nesta etapa.

## Limitações

- O cálculo guarda caches de uma janela B; memória temporária é O(B vezes dimensão do estado).
- O forward é byte a byte e o segmentador não é executado.
- A aproximação straight-through do cast FP32 pode gerar pequenas diferenças em relação à derivada numérica exata da implementação quantizada.
- O resultado ainda não demonstra que o modelo aprende linguagem: isso depende de treinamento e avaliação em corpus.
