# 13. Arquitetura matemática da RLAH

**Versão:** 0.3 — proposta matemática de referência  
**Estado:** especificação proposta; ainda não implementada nem validada  
**Escopo:** núcleo de sequência, representação em bytes, contexto multirritmo e interface com segmentação hierárquica.

## 13.1 Decisão arquitetural

A primeira família da RLAH será um **núcleo recorrente compacto de dois ritmos (fast/slow)**, com estado vetorial limitado. O núcleo não utilizará atenção global sobre todos os bytes anteriores.

A escolha é motivada por memória de inferência limitada ao estado corrente, custo por byte linear no comprimento da sequência, implementação direta em C++ e número de operações controlável em dimensões pequenas.

A arquitetura é inspirada pelo campo de modelos recorrentes e de espaço de estados, mas não é uma reprodução de Mamba, RWKV ou Mamba-3. Não se reivindica superioridade nem novidade científica.

## 13.2 Convenções

- Entrada: fluxo de bytes UTF-8 validados.
- Byte: inteiro sem sinal entre 0 e 255.
- Representação binária: vetor x_t em {-1,+1}^8, derivado dos oito bits do byte.
- Dimensão inicial de estado: d = 11 para a G1, conforme inventário de parâmetros.
- Funções: sigmoid(z)=1/(1+exp(-z)); tanh aplicada elemento a elemento; ⊙ representa produto elemento a elemento.
- h_f: estado rápido.
- h_s: estado lento.
- Todas as operações usam parâmetros compartilhados ao longo das posições.

A representação binária direta evita uma tabela de embedding de 256 entradas. Isso reduz parâmetros e elimina lookup de vocabulário, mas não prova que seja a melhor representação; deverá ser comparada com embedding compacto.

## 13.3 Atualização do estado rápido

Para cada byte t:

a_t = sigmoid(W_ax x_t + W_ah h_f(t-1) + b_a)

c_t = tanh(W_cx x_t + W_ch h_f(t-1) + b_c)

h_f(t) = a_t ⊙ h_f(t-1) + (1-a_t) ⊙ c_t

As quatro matrizes W_ax, W_ah, W_cx e W_ch têm dimensão 11×11; b_a e b_c têm dimensão 11. Total: 4×121 + 22 = 506 parâmetros.

Interpretação: a_t controla retenção do estado rápido anterior; c_t propõe novo conteúdo; a combinação realiza retenção e atualização.

## 13.4 Atualização do estado lento

O estado lento recebe o estado rápido já atualizado:

g_t = sigmoid(W_gh h_f(t) + W_gs h_s(t-1) + b_g)

h_s(t) = g_t ⊙ h_s(t-1) + (1-g_t) ⊙ h_f(t)

W_gh e W_gs têm dimensão 11×11; b_g tem dimensão 11. Total: 2×121 + 11 = 253 parâmetros.

O estado lento funciona como resumo de escala temporal maior. Não é memória ilimitada: sua capacidade é finita e pode esquecer detalhes. Informação persistente e recuperação explícita pertencem ao IA-MEM.

## 13.5 Reinicialização e limites

- No início de uma sequência independente, h_f e h_s são inicializados em zero, salvo se um estado for explicitamente transportado por uma sessão.
- Estado transportado deverá ser associado a uma sessão e nunca compartilhado acidentalmente entre usuários ou documentos.
- Valores não finitos deverão provocar erro controlado.
- O estado não deverá crescer com o comprimento do texto.
- O truncamento de gradiente será usado no treinamento em blocos, preservando estado numérico entre blocos, mas interrompendo o grafo de gradientes no limite definido.

## 13.6 Complexidade

Com dimensão d fixa, cada byte exige um número constante de operações matriciais e vetoriais. O custo total do núcleo é O(n·d²) para n bytes; o estado de inferência é O(d).

Para d=11, as matrizes são pequenas. O custo real em CPU, porém, pode ser dominado por overhead de chamadas e layout de memória. Implementação em laços contíguos e benchmarks são obrigatórios.

## 13.7 Normalização e estabilidade

A primeira versão não adicionará normalização complexa por padrão. Será usada inicialização controlada, clipping de gradiente e monitoramento de norma do estado. Uma normalização só será adicionada se experimentos mostrarem instabilidade e se o custo/benefício for positivo.

## 13.8 Alternativas rejeitadas para G1

- Atenção global: custo de memória e computação cresce com o contexto e é desnecessário para a prova inicial.
- SSM complexo completo: interessante em modelos maiores, mas introduz complexidade numérica e implementação desproporcionais para 1.000 parâmetros.
- MIMO sofisticado: benefício observado em modelos grandes não pode ser presumido em uma G1 minúscula.
- Rede separada de controle: acrescenta parâmetros e custo para uma tarefa que pode começar com regras matemáticas.

Essas alternativas poderão ser reavaliadas em gerações futuras com evidência.

## 13.9 Estado da decisão

A família de núcleo fast/slow com d=11 está selecionada para prototipagem. O inventário aritmético fecha em 1.000 parâmetros quando combinado à cabeça de predição e ao pontuador de segmentação definidos nos documentos 14 e 15. A contagem ainda precisa ser confirmada por teste automatizado na implementação.
