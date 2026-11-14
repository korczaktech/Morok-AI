# Etapa 3 — RLAH implementada (primeira versão executável)

Adiciona segmentador auxiliar de candidatos contíguos de 2–8 bytes, frequência mínima, limite de candidatos, características de 22 dimensões, associação preditiva diagnóstica por entropia com suavização add-one, estimativa de compressão, pontuador de 29 parâmetros com BCE/Adam, controlador multiplicativo limitado, peso de preservação fixo e histerese.

Adiciona RLAH-BLOCK v1: cabeçalho de 24 bytes, dicionário até 64 entradas, tokens literais/referência, CRC-32/IEEE, validação de limites e fallback bruto quando a representação não economiza espaço.

O núcleo G1 continua byte a byte. A associação preditiva é diagnóstica, não ganho causal. Esta primeira implementação ainda exige validação de integração por época/cache LRU, fuzzing e execução real de CI antes de ser considerada etapa integralmente aceita.