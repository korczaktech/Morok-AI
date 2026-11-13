# 15. Segmentação adaptativa e decisão de agrupamento

## 15.1 Princípio

A segmentação será um mecanismo limitado de seleção de unidades em múltiplas escalas. Para G1, a busca por agrupamentos será predominantemente algorítmica e não exigirá uma segunda rede neural. Um pontuador linear pequeno selecionará candidatos; o controlador adaptativo dos quatro critérios continuará algorítmico e externo.

## 15.2 Candidatos

- Nível 0: bytes individuais, sempre disponíveis.
- Nível 1: sequências contíguas candidatas de 2 a 8 bytes.
- Nível 2: combinações de unidades do nível 1, limitadas pelo orçamento de processamento.
- Profundidade máxima inicial: três níveis contando bytes.

O sistema não materializará todos os substrings possíveis. Candidatos serão extraídos de janelas locais e limitados por frequência mínima e orçamento máximo por bloco. Os limites numéricos estão no documento 18.

## 15.3 Quatro critérios

Para cada candidato g, estimar:

**Frequência F(g):** frequência suavizada e normalizada. Usar contagem com prior pequeno, transformação log1p e normalização no conjunto de candidatos do bloco.

**Ganho preditivo P(g):** na G1, é uma associação preditiva diagnóstica, não um ganho causal do modelo. Estimar a redução da entropia empírica do próximo byte nas ocorrências do candidato em relação à distribuição-base do mesmo bloco, com suavização e divisão de dados sem vazamento. A fórmula e a meta inicial estão no documento 18. Não denominar essa métrica redução de BPB end-to-end.

**Benefício de compressão C(g):** economia líquida de bits, incluindo custo real de tabela, índices, comprimentos, escapes, cabeçalhos, referências e metadados. Se o agrupamento não reduzir o tamanho total serializado, C(g)=0.

**Preservação R(g):** indicador de round-trip exato. R(g)=1 se expandir a estrutura agrupada reproduz exatamente os bytes originais; caso contrário R(g)=0. R é restrição obrigatória; R=0 rejeita o candidato antes da pontuação.

## 15.4 Pontuação multiobjetivo

U(g) = w_f F(g) + w_p P(g) + w_c C(g) + w_r R(g)

Pesos iniciais:
- w_f = 0,10;
- w_p = 0,40;
- w_c = 0,25;
- w_r = 0,25.

A soma é 1. R também é uma restrição rígida, não um atributo compensável.

## 15.5 Pontuador compacto da G1

O pontuador possui 22 coeficientes treináveis e sete vieses específicos por comprimento (2 a 8 bytes), totalizando 29 parâmetros.

O vetor de características φ(g) tem 22 dimensões:
- 4 métricas normalizadas F, P, C e R;
- 8 estatísticas binárias médias do candidato;
- 8 valores de contexto derivados das primeiras oito coordenadas do estado rápido;
- 1 comprimento normalizado;
- 1 frequência logarítmica normalizada.

S(g) = sigmoid(vᵀ φ(g) + θ_len(g))

O pontuador ordena candidatos elegíveis; não substitui o controlador. Sua perda treina somente os 29 parâmetros auxiliares.

## 15.6 Treinamento do pontuador

Os alvos são gerados pelo critério multiobjetivo determinístico:
- y=1 para candidato reversível que supera o limiar de utilidade e apresenta benefício líquido;
- y=0 para candidato reversível que não supera o limiar;
- candidatos não reversíveis são excluídos.

A perda é entropia cruzada binária calculada a partir de logits estáveis. O pontuador é atualizado em ciclos de segmentação e congelado na janela seguinte.

Na G1, L_group não atualiza os parâmetros do núcleo ou da cabeça de predição: não existe caminho diferenciável do pontuador para o preditor byte a byte. O objetivo total é a soma das perdas sobre conjuntos de parâmetros disjuntos, não uma alegação de otimização conjunta end-to-end.

## 15.7 Controlador adaptativo de pesos

Não haverá rede neural controladora. Os pesos são atualizados apenas após avaliação completa e comparável.

Regra:
w'_i = w_i · exp(η d_i)
w_i(t+1) = projeção nos limites e renormalização

d_i é o déficit normalizado em relação à meta. As metas, η, limites, tratamento especial de R e política de suspensão estão especificados numericamente no documento 18. O peso de preservação permanece fixo na G1; candidatos não reversíveis são rejeitados.

## 15.8 Aceitação e histerese

Um candidato é rejeitado se não for reversível, não cumprir comprimento/orçamento ou não superar a frequência mínima. Candidato novo só entra quando U atingir o limiar de criação; candidato ativo é removido apenas abaixo do limiar inferior de retenção. Valores iniciais constam no documento 18.

## 15.9 Cache e eficiência

- Contagens atualizadas incrementalmente.
- Hashes para evitar recálculo, com confirmação dos bytes para evitar colisões.
- Avaliação preditiva amostrada.
- Teto de candidatos por bloco e de candidatos ativos.
- Limite de memória e política LRU.
- Segmentação congelada durante cada época de treinamento.

## 15.10 Limitação de originalidade

Charformer/GBST, CANINE, ByteFlow e zip2zip já exploram aspectos de segmentação aprendida, granularidade ou compressão adaptativa. A RLAH combina escolhas específicas de reversibilidade hierárquica, pontuação multiobjetivo, controle periódico limitado e núcleo recorrente fast/slow, mas essa combinação ainda precisa de revisão bibliográfica profunda e experimentos. Não se declara novidade científica.

## 15.11 Limite explícito da G1

O preditor B0 continua consumindo bytes individuais. A hierarquia é avaliada como subsistema de representação, organização e compressão reversível, sem alegar que ela acelera ou melhora o preditor. Para testar benefício linguístico end-to-end, uma geração posterior deverá definir como unidades agrupadas entram no núcleo, como o modelo retorna a bytes e qual baseline equivalente será comparado. Essa mudança poderá alterar o inventário de parâmetros e exige nova decisão arquitetural.
