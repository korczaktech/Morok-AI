# 14. Predição binária e função objetivo

## 14.1 Predição autoregressiva de bits

Após processar o byte observado na posição i, o núcleo atualiza h_f(i) e h_s(i) e prevê o byte seguinte b_{i+1}. O byte-alvo e bytes futuros não podem participar da entrada que produz a previsão.

O byte alvo possui oito bits b_{i+1,0},...,b_{i+1,7}. A probabilidade é fatorada em oito decisões condicionais:

P(b_{i+1}|contexto) = produto_j P(b_{i+1,j}|h_f(i),h_s(i),b_{i+1,0:j-1})

A cabeça usa [h_f;h_s], dimensão 22:

p_{i+1,j} = sigmoid(o_jᵀ[h_f(i);h_s(i)] + soma_{k<j} A_{j,k}(2b_{i+1,k}-1) + c_j)

O_j compõe matriz 8×22; c tem oito valores; A contém os 28 coeficientes da triangular estrita. Total: 176+8+28=212 parâmetros. Ordem de bits: mais significativo para menos significativo.

## 14.2 Perda e BPB

Usar BCE-with-logits estável. Se BCE_mean for média em bytes e bits, BPB=8*BCE_mean/ln(2). Se for acumulada a soma das oito perdas por byte, BPB=soma_nats/(N_bytes*ln(2)). Registrar sempre a convenção usada.

Mascarar posições sem alvo. O último byte de cada sequência não gera alvo sem próximo byte.

## 14.3 Distribuição conjunta

As oito condicionais definem distribuição conjunta sobre 256 bytes. Teste obrigatório: somar as probabilidades dos 256 valores e verificar resultado próximo de 1 dentro da tolerância numérica definida no teste.

## 14.4 Baseline softmax

Implementar variante comparativa com softmax de 256 classes. Não incluir seus parâmetros no inventário oficial de 1.000; comparar em variante com orçamento próprio ou redistribuição formal. Medir BPB, latência, throughput, estabilidade e parâmetros.

## 14.5 Reconstrução

A hierarquia usa expansão determinística e referências aos bytes originais. Round-trip deve ser exato. Não existe decoder neural de reconstrução na G1.

## 14.6 Objetivo efetivo

L_total = L_pred + lambda_group * L_group.

L_pred atualiza os 971 parâmetros do núcleo e cabeça. L_group atualiza apenas os 29 parâmetros do pontuador. A soma é uma função objetivo sobre conjuntos de parâmetros disjuntos; não significa que a segmentação influencia a previsão B0.

## 14.7 Geração

Política inicial gulosa: bit 1 se logit>=0, caso contrário 0. Gerar bits do mais significativo ao menos significativo, montar byte e processá-lo para prever o seguinte. Não há EOS na G1; parar por limite de bytes ou sequência externa de parada. Validar UTF-8 estritamente antes de apresentar texto.
