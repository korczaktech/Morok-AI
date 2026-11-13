# Etapa 9 — Checkpoints e persistência

## Implementado nesta entrega
- Formato MOROKCKP v1, cabeçalho fixo de 104 bytes, little-endian.
- Magic, versão, architecture_id, contagem 1.000, FP32, flags e CRC32/IEEE do payload.
- Parâmetros na ordem canônica.
- Estados AdamW (8.000 bytes) e SGD momentum (4.000 bytes).
- Estado RNG opcional (quatro u64).
- Cursor opcional com IDs dos documentos, posição e offset.
- Metadados limitados a 64 KiB.
- Checkpoints de inferência podem omitir otimizador, RNG e cursor.
- Validação de arquitetura, versão, flags, dimensões, finitude, momentos, CRC, truncamento e bytes extras.
- Gravação em arquivo temporário, flush e rename atômico.
- Testes de round-trip para AdamW/SGD, RNG, cursor, metadados, checkpoint de inferência e corrupção via CRC.

## Pendências para concluir a etapa
- Integrar salvamento automático no treinador.
- Retomar treinamento de forma determinística após interrupção.
- Testar interrupção durante escrita e retomada ponta a ponta.
- fsync explícito de arquivo e diretório para durabilidade após queda de energia.

Apenas o codec binário está implementado nesta entrega. Não considerar a Etapa 9 concluída até que o treinador use o checkpoint e os testes de retomada estejam verdes.