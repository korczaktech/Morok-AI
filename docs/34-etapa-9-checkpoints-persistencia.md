# Etapa 9 — Checkpoints e persistência

## Objetivo

Permitir que um treinamento G1 seja interrompido e retomado sem reinicializar parâmetros, estado do otimizador ou sequência pseudoaleatória. A retomada ocorre em fronteiras de época, mantendo o uso de memória previsível.

## Formato MOROKCKP v1

- Magic de 8 bytes: `MOROKCKP`.
- Versão 1, cabeçalho fixo de 104 bytes, little-endian.
- `architecture_id`: `RLAH-G1-fastslow-d11-bit8`, preenchido com NUL até 32 bytes.
- Inventário obrigatório de 1.000 parâmetros em FP32.
- Parâmetros na ordem canônica G1.
- Estado AdamW: 1.000 valores FP32 de primeiro momento + 1.000 de segundo momento (8.000 bytes).
- Estado SGD Momentum: 1.000 valores FP32 de velocidade (4.000 bytes).
- Estado xoshiro256**: quatro u64 (32 bytes).
- Cursor opcional: IDs/índices dos documentos na ordem embaralhada, posição e offset.
- Metadados UTF-8 até 64 KiB.
- CRC32/IEEE do payload, incluindo parâmetros, otimizador, RNG, cursor e metadados.

O arquivo de inferência pode conter somente os parâmetros. Checkpoints de treinamento contêm parâmetros, estado do otimizador, RNG, cursor e metadados de validação/retomada.

## Persistência transacional

A gravação:
1. Serializa o payload e calcula CRC32.
2. Grava em arquivo temporário no mesmo diretório.
3. Faz flush do stream e `fsync` do arquivo.
4. Renomeia o temporário para o destino.
5. Faz `fsync` do diretório para persistir a alteração de nome.

O fluxo é destinado ao ambiente Linux inicial do projeto. O processo pressupõe um único escritor por caminho de checkpoint. Em caso de falha antes do rename, o checkpoint anterior permanece intacto.

## Integração com o treinador

- O treinador aceita `checkpoint_path`, `resume_from_checkpoint`, `metrics_log_path` e `best_model_path`.
- Ao finalizar, pode exportar o melhor modelo selecionado pela validação como checkpoint de inferência (somente os 1.000 parâmetros, sem estado do otimizador).
- Um checkpoint é salvo automaticamente ao final de cada época concluída.
- O checkpoint contém os parâmetros correntes, estado AdamW/SGD, RNG xoshiro256**, época, contador de atualizações, ordem de documentos de treino e posição final do cursor.
- Metadados armazenam hash do corpus, assinatura da configuração, melhor BPB de validação, melhor época, contador de paciência, contadores de segurança para sequências de gradientes extremos/zero e snapshot dos melhores 1.000 parâmetros.
- A retomada valida corpus, seed, truncation length, paciência, tipo/hiperparâmetros do otimizador, permutação e split dos documentos.
- A retomada começa na época seguinte, com o estado RNG restaurado; não reembaralha nem repete a época já concluída.
- Ao terminar, o treinador devolve os melhores parâmetros selecionados pela validação e só então calcula a métrica de teste.

A configuração de quantidade máxima de épocas pode ser aumentada entre execuções. Os demais elementos que afetam a sequência de treinamento precisam permanecer iguais.

## CLI

Treinar e gravar checkpoint:
```sh
./build/morok_train ./data/corpus 10 42 --checkpoint ./artifacts/g1.ckpt
```

Retomar e continuar gravando no mesmo arquivo:
```sh
./build/morok_train ./data/corpus 20 42 --resume ./artifacts/g1.ckpt
```

O diretório de destino do checkpoint e do log precisa existir. O corpus precisa ter pelo menos três documentos `.txt` UTF-8 únicos e cada documento tem limite configurável. O arquivo JSONL é aberto em modo append e sincronizado em disco após cada evento.

## Logs JSONL persistentes\n\nA opção `--log caminho.jsonl` grava eventos `start`, `resume`, `epoch`, `checkpoint_saved` e `finished`. Cada linha é um objeto JSON independente, contendo métricas BPB, quantidade de alvos, atualizações, norma de gradiente, época e hash do corpus. O arquivo é append-only; cada evento é flushado e sincronizado com `fsync`. Em retomadas, os eventos novos são acrescentados ao mesmo arquivo.\n\nExemplo:\n\n```sh\n./build/morok_train ./data/corpus 10 42 --checkpoint ./artifacts/g1.ckpt --log ./artifacts/treinamento.jsonl --export-best ./artifacts/g1-best.ckpt\n./build/morok_train ./data/corpus 20 42 --resume ./artifacts/g1.ckpt --log ./artifacts/treinamento.jsonl --export-best ./artifacts/g1-best.ckpt\n```\n\n## Testes automatizados

- Round-trip de parâmetros, AdamW, SGD, RNG, cursor e metadados.
- Checkpoint de inferência sem estado de treinamento.
- Corrupção de payload detectada pelo CRC.
- Validação de metadados UTF-8 e valores FP32 finitos.
- Treinamento contínuo de quatro épocas comparado com duas épocas + encerramento + retomada até quatro.\n- Igualdade exata do vetor de parâmetros e da melhor época após retomada.
- Exportação do checkpoint de inferência com os melhores parâmetros, sem estado do otimizador.\n- Logs JSONL append-only com eventos de início, retomada, época, checkpoint e finalização.
- Comparação exata do vetor final de parâmetros e melhor época entre os dois caminhos.
- Testes de treinamento existentes continuam cobrindo split, deduplicação, UTF-8, continuidade de estado e BPB.

## Limites conhecidos

- Checkpoint é produzido em fronteiras de época; não há retomada no meio de documento ou bloco.
- O arquivo guarda o cursor/ordem para auditoria e validação, mas a retomada só é permitida quando o cursor indica fim de época.
- O checksum do corpus é um hash não criptográfico de 64 bits, adequado para detectar alterações acidentais, não para adversários.
- O formato é versionado; mudanças incompatíveis exigem nova versão.
- O codec usa chamadas POSIX `fsync` e `O_DIRECTORY`, alinhadas ao alvo Linux inicial.
- Checkpoints e corpora não devem ser enviados ao Git por padrão.

## Critério de conclusão

A etapa cobre persistência binária, validação, salvamento automático, retomada determinística na fronteira de época e teste de equivalência com execução contínua. A qualidade linguística e a eficácia do modelo não são critérios desta etapa; devem ser avaliadas em treinamento experimental posterior.
