# G3 — formato de checkpoint e persistência

## Escopo

O checkpoint de treinamento é separado do artefato .morok de inferência. O artefato de inferência continua contendo apenas os pesos e metadados necessários para executar o modelo. O checkpoint preserva o estado mestre FP32 e os dados necessários para reconstruir uma sessão de treinamento.

## Formato binário

- Magic: MRK3CKPT.
- Versão do formato: 1.
- Cabeçalho: 64 bytes.
- Inteiros e floats: little-endian, sem alinhamento implícito.
- Pesos e momentos: FP32.
- Checksum: CRC-32/IEEE sobre o arquivo inteiro, considerando o campo de checksum zerado durante o cálculo.
- Limite atual de arquivo: 512 MiB.
- Arquitetura aceita: geração 3, architecture_id=0x0302, 100.000 parâmetros.

A versão do formato é independente da versão do artefato de inferência. Campos reservados precisam permanecer zerados. Uma versão desconhecida, arquitetura diferente, payload truncado ou checksum inválido é rejeitado.

## Estado persistido

G3TrainingCheckpoint armazena:
- pesos FP32;
- primeiro e segundo momentos do AdamW e contador de passos;
- passo e taxa de aprendizado do scheduler;
- época concluída, próximo batch e posição do sampler;
- ordem do sampler e índices dos splits train/validation/test;
- estado serializado dos RNGs C++, Python, NumPy e backend;
- baseline por categoria, RNG do REINFORCE e momentos do otimizador RL;
- beta KL e snapshot binário da referência SFT congelada;
- estado opaco do replay buffer e histórico de treinamento;
- run_id, fingerprint do dataset, versão do código, configuração e fingerprint de configuração.

O campo de estado opaco permite preservar estruturas específicas do backend ou do replay buffer sem acoplar o formato binário à implementação de cada módulo.

## Compatibilidade de retomada

A leitura pode receber um CheckpointCompatibility esperado. A retomada é rejeitada quando divergir qualquer um destes valores:
- geração e architecture ID;
- versão do código;
- fingerprint do dataset;
- identificador do otimizador;
- fingerprint da configuração.

A validação estrutural também verifica exatamente 100.000 pesos FP32 finitos, momentos de otimizador vazios em conjunto ou com 100.000 valores cada, taxa de aprendizado finita e positiva, beta KL válido, baselines RL no intervalo [-1,1] e limites de alocação.

Não existe retomada aproximada silenciosa. Se o chamador não puder fornecer a mesma compatibilidade, deve iniciar uma nova execução ou implementar uma migração explícita.

## Salvamento seguro

save_training_checkpoint_atomic:
1. serializa e valida o estado;
2. grava em arquivo temporário no mesmo diretório;
3. faz flush e sincronização durável (fsync no POSIX ou FlushFileBuffers no Windows);
4. relê e valida o arquivo temporário, incluindo o CRC;
5. substitui o destino por rename atômico;
6. sincroniza o diretório no POSIX quando suportado.

Falhas antes da substituição preservam o checkpoint anterior. O chamador deve manter pelo menos um checkpoint válido adicional (por exemplo last e best) e nunca apontar ambos para o mesmo único arquivo de recuperação.

## API

Cabeçalho público: morok/g3/training/checkpoint.hpp

Operações:
- serialize_training_checkpoint / deserialize_training_checkpoint;
- save_training_checkpoint_atomic / read_training_checkpoint;
- validate_training_checkpoint.

A suíte g3.checkpoint cobre round-trip dos estados SFT/RL, corrupção, truncamento, versão desconhecida, incompatibilidade de dataset/configuração/código/otimizador, valores não finitos e preservação do arquivo anterior em falha de gravação.