# 15. Segmentação adaptativa e decisão de agrupamento

## 15.1 Princípio

A segmentação será um mecanismo limitado de seleção de unidades em múltiplas escalas. Para G1, a busca por agrupamentos será predominantemente algorítmica e não exigirá uma segunda rede neural. Um pontuador linear pequeno selecionará candidatos; o controlador adaptativo dos quatro critérios continuará algorítmico e externo.

## 15.2 Candidatos

- Nível 0: bytes individuais, sempre disponíveis.
- Nível 1: sequências contíguas candidatas de 2 a 8 bytes.
- Nível 2: combinações de unidades do nível 1, com limite de comprimento total definido pelo orçamento de processamento.
- Profundidade máxima inicial: três níveis contando bytes.

O sistema não materializará todos os substrings possíveis. Candidatos serão extraídos de janelas locais e limitados por frequência mínima e orçamento máximo por bloco.

## 15.3 Quatro critérios

Para cada candidato g, estimar:

**Frequência F(g):** frequência suavizada e normalizada. Usar contagem com prior pequeno para evitar divisão por zero e favorecer estabilidade; transformar contagens por log1p e normalizar no conjunto de candidatos do bloco.

**Ganho preditivo P(g):** redução relativa da perda de validação ao processar o agrupamento em comparação com a representação básica:
P(g) = clip((L_base - L_g) / max(L_base, ε), 0, 1).

**Benefício de compressão C(g):** economia líquida de bits, incluindo custo de metadados, índices, tabela e referências:
C(g) = clip((B_base - B_grouped) / max(B_base, ε), 0, 1).
Se o agrupamento aumenta o tamanho total, C(g)=0.

**Preservação R(g):** indicador de round-trip exato. R(g)=1 se expandir a estrutura agrupada reproduz exatamente os bytes originais; caso contrário R(g)=0. R é uma restrição obrigatória, não um atributo que possa ser compensado por frequência ou previsão.

## 15.4 Pontuação multiobjetivo

U(g) = w_f F(g) + w_p P(g) + w_c C(g) + w_r R(g)

Pesos iniciais:
- w_f = 0,10;
- w_p = 0,40;
- w_c = 0,25;
- w_r = 0,25.

A soma é 1. Como R também é uma restrição rígida, candidatos com R=0 são rejeitados antes da pontuação.

## 15.5 Pontuador compacto da G1

O pontuador possui 22 coeficientes treináveis e sete vieses específicos por comprimento (2, 3, 4, 5, 6, 7 ou 8 bytes), totalizando 29 parâmetros.

O vetor de características φ(g) tem 22 dimensões:
- 4 métricas normalizadas F, P, C e R;
- 8 estatísticas binárias médias do candidato, uma por posição de bit;
- 8 valores de contexto derivados das primeiras oito coordenadas do estado rápido;
- 1 comprimento normalizado;
- 1 frequência logarítmica normalizada.

A pontuação é:
S(g) = sigmoid(v^T φ(g) + θ_len(g))

onde v possui 22 valores e θ possui sete vieses, um por comprimento candidato.

A pontuação linear é usada para ordenar candidatos elegíveis. A preservação exata continua sendo uma condição obrigatória. O pontuador não substitui o controlador adaptativo dos pesos F/P/C/R.

## 15.6 Controlador adaptativo de pesos

Não haverá uma rede neural controladora. Os pesos serão atualizados apenas entre blocos de avaliação, não a cada byte.

Regra multiplicativa proposta:
w'_i = w_i · exp(η d_i)
w_i(t+1) = clip_normalize(w'_i, w_min, w_max)

d_i representa o déficit normalizado do critério em relação à meta experimental. Déficit maior aumenta a atenção daquele critério. Depois do clipping, os pesos são renormalizados para somar 1.

Limites iniciais:
- w_min = 0,05;
- w_max = 0,60;
- frequência com teto adicional de 0,20;
- η pequeno, calibrado por experimento.

A atualização será suspensa se métricas forem insuficientes, instáveis ou não comparáveis. Metas e definição operacional de d_i deverão ser registradas em cada experimento.

## 15.7 Aceitação e histerese

Um candidato será rejeitado se não for reversível, não cumprir comprimento ou orçamento, ou não superar a frequência mínima. Será aceito somente se sua utilidade superar o limiar de criação. Será mantido enquanto permanecer acima do limiar inferior de retenção.

O limiar de criação será maior que o limiar de retenção. Os limiares numéricos serão calibrados em validação.

## 15.8 Cache e eficiência

- Contagens serão atualizadas incrementalmente.
- Hashes de candidatos evitarão recalcular sequências já vistas.
- Avaliação preditiva será amostrada, não executada para todo candidato.
- O número de candidatos por bloco terá teto configurável.
- Tabelas terão limite de memória e política de remoção.
- A segmentação será congelada durante cada janela de treinamento.

## 15.9 Limitação de originalidade

Charformer/GBST, CANINE, ByteFlow e zip2zip já exploram aspectos de segmentação aprendida, granularidade ou compressão adaptativa. A RLAH combina escolhas específicas de reversibilidade hierárquica, pontuação multiobjetivo, controle periódico limitado e núcleo recorrente fast/slow, mas essa combinação ainda precisa de revisão bibliográfica profunda e experimentos. Não se declara novidade científica.
