# 16. Protocolo matemático de treinamento

## 16.1 Objetivo

Treinar os parâmetros do núcleo de sequência para minimizar a perda de predição de bits. A segmentação e o controlador serão atualizados em etapas separadas, para evitar que a estrutura de entrada mude durante cada atualização do modelo.

## 16.2 Ordem de operações

Para cada documento:
1. validar ou aplicar a política de entrada UTF-8;
2. converter os dados em bytes;
3. gerar vetores binários de oito dimensões;
4. inicializar ou restaurar os estados fast/slow;
5. processar bytes em ordem causal;
6. prever o próximo byte;
7. calcular perda binária por bit;
8. acumular gradientes em blocos;
9. aplicar clipping de gradiente;
10. atualizar parâmetros;
11. registrar métricas e estado do experimento.

## 16.3 Otimizador inicial

Escolha inicial: SGD com momentum ou Adam? Para o primeiro baseline, utilizar AdamW simplificado somente se o custo de estados auxiliares couber no orçamento de RAM. Como G1 tem poucos parâmetros, Adam pode acelerar convergência, mas duplica ou triplica armazenamento de estados por parâmetro.

A especificação inicial seleciona **AdamW** para experimentos de qualidade e **SGD com momentum** como baseline de baixo estado. A implementação deverá comparar convergência, RAM e tempo. Não há hiperparâmetros finais aprovados.

## 16.4 Truncamento de gradiente

O fluxo será dividido em blocos de comprimento B. O estado numérico final de um bloco será a inicialização do próximo; o grafo de gradientes será interrompido entre blocos (truncated BPTT).

B deverá ser pequeno o suficiente para respeitar RAM, mas grande o suficiente para permitir dependências úteis. Valores de teste iniciais: 32, 64 e 128 bytes, comparados em benchmark.

## 16.5 Inicialização

Usar inicialização determinística baseada em semente. Matrizes recorrentes devem começar com escala controlada; vieses dos gates poderão ser inicializados para favorecer retenção moderada, evitando saturação inicial. A regra final deverá ser documentada e testada quanto a gradientes não nulos e estabilidade.

## 16.6 Agendamento de segmentação

A segmentação será avaliada entre épocas ou após blocos de treinamento definidos. Quando o conjunto de agrupamentos mudar:
- registrar versão da segmentação;
- congelar a nova versão durante a janela seguinte;
- invalidar caches derivados;
- manter possibilidade de retomar a configuração anterior;
- comparar a perda antes/depois da alteração.

## 16.7 Reprodutibilidade

A execução deverá registrar semente, ordem dos dados, versão de corpus, versão de arquitetura, parâmetros, otimizador, hiperparâmetros, configuração de segmentação, versão do compilador e métricas.

## 16.8 Critério de estabilidade

Interromper ou marcar experimento como falho se houver NaN/Inf, gradiente persistentemente nulo, explosão de norma, corrupção de checkpoint ou divergência de reconstrução. A falha deve gerar diagnóstico, não ser silenciosamente ignorada.
