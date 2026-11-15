# 16. Protocolo matemático de treinamento

**Estado:** protocolo de referência G1; hiperparâmetros iniciais detalhados no documento 18.

## 16.1 Objetivo

Treinar os parâmetros do núcleo de sequência para minimizar a perda de predição do próximo byte. A segmentação e seu controlador são atualizados em etapas separadas. A segmentação G1 é auxiliar e não substitui bytes no caminho preditivo B0.

## 16.2 Ordem causal de operações

Para cada documento:
1. validar UTF-8 estritamente conforme documento 18;
2. converter o documento em bytes;
3. dividir em blocos de leitura sem quebrar a ordem;
4. inicializar ou restaurar os estados fast/slow;
5. para cada byte observado na posição i, construir o vetor de 11 características definido no documento 13;
6. atualizar os estados fast/slow com o byte i;
7. prever o byte i+1 usando os estados atualizados;
8. calcular a perda contra o byte real i+1, bit a bit;
9. acumular gradientes durante o bloco BPTT;
10. aplicar clipping global;
11. atualizar parâmetros;
12. transportar os valores numéricos do estado para o próximo bloco, interrompendo o grafo de gradientes na fronteira;
13. registrar métricas e estado do experimento.

O último byte de cada sequência não gera alvo se não existir byte seguinte. O alvo nunca pode participar da entrada que produz sua própria previsão.

## 16.3 Entrada e alvos

A entrada do núcleo tem 11 dimensões: oito bits codificados em {-1,+1}, paridade, contagem normalizada de bits 1 e valor do byte normalizado. Os oito bits do alvo são usados pela cabeça de predição; não substituir a entrada 11D por um vetor 8D.

## 16.4 Otimizadores

AdamW é o otimizador principal do primeiro baseline. Padrão inicial: learning rate 0,001; beta1 0,9; beta2 0,999; epsilon 1e-8; weight decay 0,0001, aplicado a matrizes/pesos e não aos vieses.

SGD com momentum é baseline comparativo: learning rate 0,01; momentum 0,9; weight decay 0,0001. Não misturar estados de otimizadores em uma mesma execução.

A busca inicial de learning rate e a configuração B0 estão detalhadas no documento 18. Nenhum valor é considerado ótimo antes de comparação em validação.

## 16.5 Truncamento de gradiente

O fluxo será dividido em blocos B. Padrão inicial B=64 bytes, comparando B=32 e B=128. O estado numérico final de um bloco é usado como estado inicial do próximo, mas o grafo de gradientes é interrompido na fronteira (truncated BPTT).

## 16.6 Inicialização e estabilidade

Usar inicialização determinística baseada em semente. Matrizes de entrada e cabeça usam Xavier uniform; matrizes recorrentes usam inicialização ortogonal. Vieses iniciais dos gates: rápido 0,0 e lento +1,0. Monitorar saturação, normas de estado e gradientes.

Interromper atualização diante de NaN/Inf. Aplicar clipping pela norma global 1,0. Os demais gatilhos de parada e early stopping constam no documento 18.

## 16.7 Agendamento de segmentação

A segmentação é avaliada ao fim de cada época. Quando a estrutura mudar:
- registrar versão e métricas;
- congelar a nova versão durante a época seguinte;
- invalidar caches derivados;
- preservar checkpoint anterior;
- verificar round-trip;
- não afirmar ganho de BPB do preditor enquanto agrupamentos não fizerem parte do caminho preditivo.

## 16.8 Reprodutibilidade

Registrar semente, ordem dos dados, versão e hash do corpus, divisão, versão de arquitetura, inventário, parâmetros, otimizador, hiperparâmetros, versão do compilador, flags de build, hardware, configuração de segmentação e métricas.

## 16.9 Critério de estabilidade

Marcar experimento como falho se houver NaN/Inf, gradiente persistentemente nulo, explosão de norma, corrupção de checkpoint, quebra de causalidade ou divergência de reconstrução. A falha deve produzir diagnóstico e preservar o último checkpoint válido.
