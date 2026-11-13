# Etapa 8 — Pipeline de treinamento da G1

**Objetivo:** conectar corpus UTF-8, BPTT e otimizadores em um ciclo executável, com divisão por documento, métricas e avaliação isolada.

## Implementado
- Descoberta recursiva de arquivos `.txt`.
- Validação UTF-8 estrita e hash em passagem anterior ao treino; documento inválido interrompe antes de qualquer atualização.
- Leitura em fluxo com buffer de 64 KiB; corpus integral não é carregado na RAM.
- Deduplicação exata por FNV-1a-64 e comparação integral de bytes quando há colisão de hash.
- Split determinístico por documento: 90/5/5 para pelo menos 20 documentos; em corpora de 3–19 documentos, N-2/1/1 para garantir os três conjuntos.
- Embaralhamento determinístico de documentos de treino a cada época; bytes nunca são embaralhados.
- Reset dos estados fast/slow por documento.
- BPTT em janelas de no máximo B+1 bytes, com um byte de sobreposição e estado numérico transportado; gradientes truncados na fronteira.
- AdamW ou SGD momentum atualizados após cada janela com alvos.
- Métricas de treino e validação em BPB, early stopping e seleção do melhor estado em memória.
- Teste final executado somente após seleção pela validação.
- CLI `morok_train <corpus-directory> [epochs] [seed]`, saída JSONL.

## Contrato incremental
A função BPTT recebe o estado imediatamente anterior ao primeiro byte da janela. O estado retornado fica antes de consumir o último byte-alvo. A próxima janela começa com esse último byte como contexto e recebe o estado retornado. Assim a perda do byte de sobreposição não é repetida e a sequência causal é preservada.

## Limites e exclusões
- B padrão 64; blocos B=32/64/128 podem ser configurados.
- Um fluxo recorrente por vez e um worker de treinamento.
- Metadados limitados por `max_documents` (100.000); cada documento limitado por `max_document_bytes` (1 GiB).
- Corpus é relido em passagens para manter baixo uso de RAM.
- FNV-1a-64 é identificador não criptográfico, não mecanismo de integridade adversarial.
- Melhor snapshot permanece em RAM. Checkpoints retomáveis e logs persistentes são Etapa 9.
- O segmentador de 29 parâmetros não participa do treinamento B0.
- O limite de RSS de 512 MiB é meta de projeto, não medição obtida nesta etapa.

## Testes
- [x] Split determinístico 18/1/1 em 20 documentos.
- [x] Atualização do otimizador e métricas BPB finitas.
- [x] Rejeição de UTF-8 inválido antes do treino.
- [ ] CI GCC/Clang Debug/Release.
- [ ] ASan/UBSan.
- [ ] Benchmark de RSS/throughput em máquina de 4 GB.
- [ ] Treino real com corpus licenciado.

A implementação do pipeline não demonstra que a G1 já aprendeu linguagem. Essa conclusão depende de treino real, metadados do corpus, métricas e avaliação de amostras.
