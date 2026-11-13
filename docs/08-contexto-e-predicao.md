# 8. RLAH — Contexto recorrente e predição

## 8.1 DEC-008 — Contexto recorrente compacto

**Decisão selecionada:** investigar um núcleo recorrente compacto em vez de atenção convencional global na primeira arquitetura.

Fluxo conceitual:
1. receber unidade;
2. incorporar informação ao estado interno;
3. atualizar o estado;
4. processar a próxima unidade;
5. utilizar o estado para previsão.

O estado terá dimensão limitada. A memória persistente de longo prazo pertence ao IA-MEM, não ao estado recorrente.

## 8.2 Motivação de eficiência

Um estado recorrente compacto pode evitar armazenar e comparar todas as posições anteriores a cada passo. Isso pode reduzir memória de contexto e custo por sequência.

Essa vantagem é uma hipótese. Recorrência pode limitar paralelismo e dificultar dependências distantes. O custo real deverá ser comparado com baselines adequados.

## 8.3 DEC-009 — Predição de bytes

A primeira geração terá como objetivo principal estimar o próximo byte condicionado ao contexto:

P(b_t | b_<t)

Foi proposta uma saída binária sequencial: cada byte contém oito bits, e o modelo estima os bits condicionados aos bits anteriores e ao contexto.

A forma exata de fatoração, ordem dos bits, codificação de saída, cálculo da perda e dimensões do cabeçalho de saída ainda precisam ser especificados.

A escolha visa reduzir a dimensão da saída em relação a uma camada categórica ampla, mas não se presume que seja sempre mais rápida: serão medidos número de operações, dependências sequenciais e latência.

## 8.4 UTF-8 e reconstrução

A geração de bytes deve produzir sequências que possam ser decodificadas como UTF-8 quando o contrato da tarefa exigir texto válido. O tratamento de bytes inválidos deverá ser explícito.

A reconstrução sem perdas da entrada é uma exigência para os caminhos de compressão/reconstrução, mas não deve ser confundida com a tarefa probabilística de geração.

## 8.5 Pendências

- Definir dimensão do estado recorrente.
- Definir equações de atualização.
- Definir inicialização e reset de estado.
- Definir truncamento de gradiente, se necessário.
- Definir fatoração binária da saída.
- Definir função de perda de bits.
- Definir amostragem e decodificação.
- Comparar com predição categórica de 256 bytes e outros baselines.
