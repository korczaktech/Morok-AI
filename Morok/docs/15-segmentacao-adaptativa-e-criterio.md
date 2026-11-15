# 15. Segmentação adaptativa e decisão de agrupamento

## 15.1 Princípio

A segmentação seleciona unidades em múltiplas escalas. Na G1, a busca é predominantemente algorítmica e usa um pontuador linear auxiliar de 29 parâmetros. O controlador adaptativo permanece algorítmico.

## 15.2 Candidatos

- Nível 0: bytes individuais.
- Nível 1: sequências contíguas de 2 a 8 bytes.
- Nível 2: combinações limitadas de unidades de nível 1.
- Profundidade máxima inicial: três níveis contando bytes.

Não materializar todos os substrings. Gerar candidatos de janelas locais e aplicar limites do documento 18.

## 15.3 Critérios de utilidade

**Frequência F(g):** frequência suavizada e normalizada com log1p.

**Associação preditiva P(g):** redução relativa da entropia empírica do próximo byte nas ocorrências do candidato, comparada à distribuição global do mesmo bloco. A definição exata com suavização add-one está no documento 18. É diagnóstico estatístico, não ganho causal do preditor.

**Compressão C(g):** economia líquida incluindo tabela, índices, comprimentos, escapes, cabeçalhos e referências. Se o tamanho serializado total não diminuir, C=0.

**Preservação R(g):** indicador de round-trip exato. R=1 é obrigatório; R=0 rejeita o candidato antes de pontuação.

## 15.4 Pontuação multiobjetivo

U(g) = w_f F(g) + w_p P(g) + w_c C(g) + w_r R(g)

Pesos iniciais: F=0,10; P=0,40; C=0,25; R=0,25. R permanece peso fixo e também condição rígida. A política de atualização de F/P/C está no documento 18.

## 15.5 Pontuador compacto

O pontuador possui 22 coeficientes treináveis e sete vieses por comprimento (2 a 8 bytes), totalizando 29 parâmetros.

O vetor φ(g) possui exatamente 22 características:
- F, P e C: 3;
- médias dos oito bits do candidato: 8;
- oito coordenadas do estado rápido: 8;
- comprimento normalizado: 1;
- diversidade interna de bytes: 1;
- maior sobreposição prefixo/sufixo normalizada: 1.

R não entra em φ porque candidatos com R=0 são eliminados e todos os exemplos restantes têm R=1. Frequência aparece uma única vez, em F.

S(g) = sigmoid(vᵀ φ(g) + θ_len(g))

## 15.6 Treinamento do pontuador

Os alvos são gerados por U e pelos limiares de criação/retenção. Candidatos não reversíveis são excluídos. A perda usa BCE-with-logits estável. A atualização ocorre apenas após avaliação completa, com uma passagem e um passo AdamW inicial; menos de 16 exemplos rotulados suspendem a atualização.

O pontuador treina somente seus 29 parâmetros. L_group não retropropaga para o núcleo ou para a cabeça de predição.

## 15.7 Controlador adaptativo

Regra multiplicativa: w'_i = w_i · exp(η d_i). Os pesos F/P/C são projetados com soma 0,75, limites [0,05;0,60] e F<=0,20. R permanece em 0,25. A projeção determinística e a definição de déficit constam no documento 18. Atualização apenas entre avaliações comparáveis.

## 15.8 Aceitação e histerese

Rejeitar candidatos sem round-trip, fora dos limites ou abaixo da frequência mínima. Candidato novo entra quando U>=U_create; candidato ativo é removido quando U<U_keep. Valores iniciais constam no documento 18.

## 15.9 Cache e eficiência

- contagens incrementais;
- hashes com confirmação byte a byte;
- avaliação amostrada;
- limites de candidatos e cache;
- remoção LRU determinística;
- segmentação congelada durante a época.

## 15.10 Limitação de originalidade

Byte-level modeling, segmentação aprendida, compressão adaptativa e recorrência possuem precedentes. A RLAH é uma combinação experimental específica, não uma reivindicação de novidade científica.

## 15.11 Limite explícito da G1

O preditor B0 continua consumindo bytes individuais. A hierarquia é avaliada como subsistema auxiliar de representação e compressão reversível. Não atribuir ganhos de BPB ou latência end-to-end à segmentação. Uma geração integrada deverá especificar como unidades agrupadas entram no núcleo e deverá ser comparada em experimento próprio.
