# 7. RLAH — Segmentação adaptativa e hierarquia

## 7.1 DEC-002 — Unidades hierárquicas

**Decisão aprovada:** manter múltiplas escalas simultaneamente, sem substituir completamente as unidades menores.

Níveis:
- Nível 1: bytes ou unidades básicas;
- Nível 2: agrupamentos intermediários;
- Nível 3: estruturas maiores.

A profundidade inicial proposta é de até três níveis. Essa escolha é provisória e deverá ser validada contra custo, qualidade e complexidade.

## 7.2 DEC-006 — Candidatos de agrupamento

A proposta inicial limita agrupamentos básicos a sequências de 2 a 8 bytes. Esse intervalo é um ponto de partida experimental, não uma restrição definitiva para todos os níveis.

O sistema deverá:
- gerar candidatos locais;
- limitar candidatos avaliados;
- pontuar candidatos;
- aceitar agrupamentos úteis;
- manter ligação com os componentes originais;
- impedir crescimento descontrolado de estruturas.

## 7.3 Reconstrução

A representação deverá preservar informação suficiente para reconstruir os bytes de origem exatamente quando a tarefa exigir reconstrução sem perdas.

Uma unidade maior deverá poder ser expandida em unidades menores. O formato de referência, os índices e o armazenamento de relações entre níveis ainda precisam ser definidos.

## 7.4 Atualização periódica

A segmentação não será reorganizada a cada byte. A política inicial é reavaliar agrupamentos em intervalos ou lotes, reduzindo custo e instabilidade.

A frequência de atualização será medida. Atualizações devem ser determinísticas quando executadas com a mesma semente, dados e configuração.

## 7.5 Prevenção de patologias

- **Agrupamento excessivo:** limitar tamanho e exigir ganho útil.
- **Fragmentação excessiva:** penalizar unidades sem benefício mensurável.
- **Perda de informação:** impor verificação de reconstrução.
- **Oscilação:** usar histerese e janela de estabilidade.
- **Explosão de candidatos:** impor orçamento de busca.
- **Custo de metadados:** incluir referências e índices no cálculo de compressão.

## 7.6 Pendências

A estrutura de dados hierárquica, o algoritmo de geração de candidatos, o orçamento máximo de candidatos e a regra de atualização ainda não foram formalizados.
