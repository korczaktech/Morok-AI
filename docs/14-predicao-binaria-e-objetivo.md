# 14. Predição binária e função objetivo

## 14.1 Predição autoregressiva de bits

O alvo é o byte seguinte ao byte observado. Após processar o byte na posição i e atualizar os estados h_f(i) e h_s(i), a cabeça prevê b_{i+1}, representado por oito bits b_{i+1,0},...,b_{i+1,7}. Essa convenção causal é obrigatória.

A probabilidade do byte será fatorada em oito decisões binárias condicionais:

P(b_{i+1} | contexto) = produto, para j=0..7, de P(b_{i+1,j} | h_f(i), h_s(i), b_{i+1,0:j-1})

A cabeça utiliza a concatenação [h_f;h_s], de dimensão 22:

p_{i+1,j} = sigmoid(o_j^T [h_f(i); h_s(i)] + soma_{k<j} A_{j,k}(2b_{i+1,k}-1) + c_j)

A matriz O tem dimensão 8×22 e o vetor c tem dimensão 8. A matriz A contém 28 coeficientes da parte triangular estrita 8×8. Total: 176 + 8 + 28 = 212 parâmetros.

A ordem dos bits é do mais significativo para o menos significativo. A geração prevê e decide um bit por vez, disponibilizando bits anteriores do mesmo byte à decisão seguinte.

## 14.2 Perda preditiva

Para bits-alvo y_j em {0,1}, utilizar entropia cruzada binária calculada a partir de logits:

L_pred = - média sobre bytes-alvo e bits de [y log(sigmoid(z)) + (1-y) log(1-sigmoid(z))]

A implementação deverá usar BCE-with-logits estável, sem calcular log(0) diretamente. Padding e posições sem alvo serão mascarados.

A métrica será bits por byte (BPB): soma da perda em bits dividida pelo número de bytes-alvo. Quando a perda estiver em nats, dividir por ln(2). Para avaliar um modelo autoregressivo, acumular a perda de todos os oito bits de cada byte.

## 14.3 Normalização da distribuição de byte

As oito condicionais definem uma distribuição conjunta fatorada sobre os 256 bytes possíveis. A soma das probabilidades dos 256 bytes deve ser aproximadamente 1, dentro da tolerância numérica definida nos testes. Esse teste detecta erros de indexação e condicionamento.

## 14.4 Comparação com softmax de 256 classes

A saída binária reduz a cabeça para 212 parâmetros, mas exige oito decisões sequenciais e não garante menor latência. Implementar baseline softmax de 256 classes em variante experimental separada. Não inserir sua cabeça no orçamento G1 de 1.000 parâmetros; comparar qualidade e velocidade em configuração com orçamento próprio ou redistribuição formal de parâmetros.

Registrar parâmetros da cabeça, operações estimadas, latência por byte, throughput, BPB e estabilidade.

## 14.5 Objetivo de reconstrução

A representação hierárquica deverá ser reversível no modo sem perdas. A primeira implementação usa referências aos bytes originais e expansão determinística dos agrupamentos. A reconstrução exata é verificada por igualdade byte a byte.

Não há decoder neural de reconstrução na G1. Se uma geração futura introduzir compressão latente com perda, deverá existir objetivo de reconstrução separado e avaliação explícita da distorção.

## 14.6 Função objetivo efetiva

Para a G1:

L_total = L_pred + lambda_group * L_group

L_group treina o pontuador auxiliar de candidatos. Como o pontuador não está no caminho do preditor B0, seus gradientes atualizam somente os 29 parâmetros de segmentação; L_pred atualiza os 971 parâmetros do núcleo e da cabeça. Não alegar que L_group melhora o preditor sem uma variante integrada e experimento que demonstre isso.

A reconstrução sem perdas é uma restrição rígida de elegibilidade: se round-trip falhar, o candidato é rejeitado. Não é uma perda aproximada treinável na G1.

## 14.7 Geração

A política inicial é gulosa e determinística: bit 1 quando logit >= 0, caso contrário 0. Bits são gerados em ordem do mais significativo ao menos significativo. O byte resultante é anexado ao contexto e processado para prever o próximo.

Amostragem com temperatura só será habilitada após o baseline estável, com RNG e semente explícitos. Não há token EOS dedicado na G1; a parada é controlada por limite de bytes ou sequência de parada do chamador. A saída deve passar por validação UTF-8 estrita antes de ser apresentada como texto. Bytes inválidos não serão descartados silenciosamente.
