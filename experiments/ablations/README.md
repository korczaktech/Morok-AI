# Ablações da G1

Esta pasta organiza experimentos que removem ou alteram um componente por vez para estimar sua contribuição. Ainda não há resultados de ablação registrados aqui.

## Regras de desenho

- Definir uma configuração de referência e congelar seu commit, corpus e split.
- Alterar apenas um fator por comparação.
- Manter arquitetura, número de atualizações, orçamento de tokens/bytes e critério de checkpoint equivalentes sempre que tecnicamente possível.
- Usar as mesmas sementes de inicialização entre referência e variante.
- Executar múltiplas sementes e publicar todas as execuções.
- Se uma ablação alterar o número de parâmetros, informar a diferença e não atribuir o efeito somente ao componente removido.
- Verificar que a variante continua válida e treinável; uma variante quebrada não é evidência sobre qualidade.

## Perguntas candidatas

1. Estado fast/slow: comparar a referência com uma variante que desabilite um dos ritmos, mantendo explícito o ajuste de parâmetros.
2. BPTT: comparar janelas de 32, 64 e 128 passos sob o mesmo orçamento de treino.
3. Segmentação RLAH: comparar caminho auxiliar habilitado/desabilitado somente quando a integração permitir uma intervenção isolada.
4. Inicialização: comparar inicializações determinísticas alternativas mantendo a mesma distribuição e escala, quando definidas formalmente.

Essas são hipóteses de trabalho, não funcionalidades já implementadas nem resultados observados. Não simular flags inexistentes no executável atual. Para cada variante que ainda não possua suporte no código, registrar primeiro a necessidade de implementação.

## Relato mínimo

Para cada variante, informar hash de código, manifesto, sementes, BPB de validação/teste, bytes-alvo, tempo, throughput, RSS, número de parâmetros e motivo de qualquer desvio. A seleção do checkpoint usa validação; o teste é avaliado uma única vez após a seleção.
