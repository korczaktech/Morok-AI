# 14. Predição binária e função objetivo

## 14.1 Predição autoregressiva de bits

O alvo primário é o próximo byte b_t, representado por oito bits b_{t,0},...,b_{t,7}. A probabilidade do byte será fatorada em oito decisões binárias condicionais:

P(b_t | contexto) = produto, para j=0..7, de P(b_{t,j} | h_f, h_s, b_{t,0:j-1})

A cabeça utiliza a concatenação [h_f;h_s], de dimensão 22:

p_{t,j} = sigmoid(o_j^T [h_f; h_s] + soma_{k<j} A_{j,k}(2b_{t,k}-1) + c_j)

A matriz O tem dimensão 8×22 e o vetor c tem dimensão 8. A matriz A contém 28 coeficientes da parte triangular estrita 8×8. Total da cabeça: 176 + 8 + 28 = 212 parâmetros.

A ordem dos bits será inicialmente do mais significativo para o menos significativo e deverá ser registrada no formato do experimento.

## 14.2 Perda preditiva

Para bits-alvo y_{t,j} em {0,1}, utilizar entropia cruzada binária:

L_pred = - média sobre t,j de [y log(p) + (1-y) log(1-p)]

O cálculo numérico deverá usar logits e uma implementação estável de binary cross-entropy-with-logits, evitando log(0). O treinamento deverá mascarar padding e posições sem alvo.

A métrica comparável será bits por byte (BPB):

BPB = soma das perdas em bits / número de bytes-alvo

Quando a perda for calculada em nats, dividir por ln(2) para converter em bits.

## 14.3 Por que não escolher softmax de 256 classes inicialmente?

A saída binária utiliza oito decisões por byte e reduz a camada de saída. Entretanto, ela pode exigir dependências sequenciais entre bits e não garante menor latência. O baseline de 256 classes deverá ser implementado para comparação.

A comparação deverá registrar parâmetros da cabeça, operações por byte, latência por byte, BPB e estabilidade de treinamento.

## 14.4 Objetivo de reconstrução

A representação hierárquica deverá ser reversível no modo sem perdas. A primeira implementação usará referências aos bytes originais e expansão determinística dos agrupamentos. A reconstrução exata será verificada por igualdade byte a byte.

Como a reconstrução é estruturalmente sem perdas, não será necessário treinar um decoder neural para o caminho de round-trip da G1. Isso evita parâmetros e custo desnecessários.

Se uma geração futura introduzir compressão latente com perda, deverá existir um objetivo de reconstrução separado e uma avaliação explícita da distorção.

## 14.5 Função objetivo efetiva

Para a G1:

L_total = L_pred + λ L_group

L_group é a penalidade de representação definida no documento 15. A reconstrução sem perdas é uma restrição de elegibilidade, não uma perda aproximada: se round-trip falhar, o candidato é rejeitado.

O termo antigo μ L_reconstrução fica reservado para uma futura representação latente com decoder aprendido. Não será usado como termo treinável na G1, porque a reconstrução exata será garantida pelo formato reversível.

## 14.6 Geração de texto

Na inferência, os bits serão amostrados ou selecionados sequencialmente conforme a política configurada. A sequência de bytes resultante deverá passar por validação UTF-8 antes de ser apresentada como texto.

Política inicial: geração gulosa determinística para testes e amostragem com temperatura configurável apenas após o baseline estar estável. Bytes inválidos não serão silenciosamente descartados; serão rejeitados ou tratados por uma política explícita do chamador.
