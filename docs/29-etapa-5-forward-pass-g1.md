# Etapa 5 — Forward pass da G1

**Objetivo:** executar as equações do núcleo recorrente fast/slow e a cabeça autoregressiva de oito bits, com causalidade estrita. Esta etapa não treina parâmetros nem calcula gradientes.

## Entrada byte a byte

\`encode_g1_byte(byte)\` produz 11 características fixas, sem parâmetros treináveis:

1. Oito bits em ordem MSB-first, codificados em -1/+1.
2. Paridade: +1 para quantidade par de bits 1; -1 para ímpar.
3. Popcount normalizado: popcount(byte)/4 - 1.
4. Valor normalizado: byte/127.5 - 1.

A função cobre todos os valores de 0 a 255.

## Estado recorrente

\`G1State\` mantém dois vetores FP32 de dimensão 11. O estado é explícito e pertence à sequência/sessão do chamador. \`reset()\` zera ambos; o chamador deve usá-lo no início de cada documento independente.

Para cada byte observado, \`advance_g1_state\` calcula em precisão de acumulação double e armazena o novo estado em FP32:

\[
a_t=\sigma(W_{ax}x_t+W_{ah}h_f(t-1)+b_a)
\]
\[
c_t=\tanh(W_{cx}x_t+W_{ch}h_f(t-1)+b_c)
\]
\[
h_f(t)=a_t\odot h_f(t-1)+(1-a_t)\odot c_t
\]
\[
g_t=\sigma(W_{gh}h_f(t)+W_{gs}h_s(t-1)+b_g)
\]
\[
h_s(t)=g_t\odot h_s(t-1)+(1-g_t)\odot h_f(t)
\]

O estado lento usa o estado rápido já atualizado. A atualização é transacional: os vetores são calculados em temporários e só copiados ao estado após todos os resultados serem finitos.

## Cabeça de predição binária

Após consumir o byte observado, a cabeça calcula oito logits-base:

\[
z=O[h_f;h_s]+c
\]

Para cada bit j, MSB-first:

\[
\ell_j=z_j+\sum_{k<j}A_{j,k}(2b_k-1)
\]
\[
p_j=\sigma(\ell_j)
\]

O índice triangular compacto de \`A\` é \`j*(j-1)/2+k\`, com j=1..7 e k=0..j-1. A cabeça usa somente bits anteriores do byte-alvo. Não há acesso a bytes futuros.

\`predict_g1_next_byte\` faz decodificação gulosa: escolhe bit 1 se logit >= 0, caso contrário 0, e usa os bits já escolhidos como prefixo para a próxima decisão. Portanto, em empate exato (logit zero), a política escolhe bit 1.

\`evaluate_g1_next_byte\` calcula a soma das oito BCE-with-logits em nats para um byte-alvo conhecido. Os bits do alvo são usados apenas para condicionar as decisões posteriores; o estado recorrente não é modificado. A soma das probabilidades dos 256 bytes deve ser 1, salvo tolerância numérica.

## API implementada

- \`encode_g1_byte\`: codificação fixa de entrada.
- \`G1State\`: estado explícito, FP32, com reset.
- \`advance_g1_state\`: consome exatamente um byte observado.
- \`predict_g1_next_byte\`: produz logits, probabilidades condicionais e byte guloso.
- \`evaluate_g1_next_byte\`: calcula NLL do próximo byte em nats.

A segmentação (29 parâmetros) permanece fora do caminho preditivo B0, conforme decisão da arquitetura. Os 1.000 parâmetros continuam no inventário; esta etapa executa os 971 parâmetros do núcleo/cabeça. Os parâmetros do pontuador de segmentação serão usados quando a etapa de segmentação for implementada.

## Precisão e falhas

- Parâmetros e estados armazenados em FP32.
- Acumulação das somas matriciais e cálculo das ativações em double para reduzir erro numérico em dimensões pequenas.
- Estado de entrada, estado de saída, logits e perda não finitos provocam erro controlado.
- A função de atualização não modifica o estado se detectar falha antes do commit.
- O chamador controla reinicialização e isolamento por documento/sessão.

## Testes de aceitação

- [x] Codificação de bytes e ordem MSB-first.
- [x] Paridade e normalização das 11 características.
- [x] Atualização fast/slow determinística.
- [x] Reset de estado.
- [x] Rejeição de estado não finito.
- [x] Predição gulosa dos oito bits e regra de desempate.
- [x] Causalidade e ausência de mutação durante scoring do alvo.
- [x] Distribuição conjunta normalizada nos 256 bytes, inclusive com dependências condicionais.
- [x] Rede responde a entrada com parâmetros inicializados.
- [x] CI GCC/Clang Debug/Release aprovado no run 43517463554.
- [x] ASan/UBSan aprovado no run 43517689716.

## Fora do escopo

Backpropagation, BPTT, gradientes, AdamW/SGD, treinamento, amostragem estocástica, validação de corpus UTF-8, sessão de conversa, memória e aplicativo.
