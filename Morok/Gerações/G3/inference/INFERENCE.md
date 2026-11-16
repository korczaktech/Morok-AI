# G3 — inferência, sessões, INT8 e benchmark

## Motor Morok

O G3 implementa o contrato neutro `morok::model::ModelBackend`. O motor comum é responsável pelo ciclo de sessão e o G3 fornece o backend GRU. O chamador é dono do buffer de estado; estados de sessões diferentes nunca são compartilhados.

## Decodificação

- Temperatura padrão: 0,7.
- Top-K padrão: 10; zero desativa o filtro.
- Softmax estável calculado somente sobre os candidatos Top-K.
- Tokens de controle PAD, BOS, USR e BOT não podem ser emitidos; EOS encerra a resposta.
- A seleção usa buffers fixos para o vocabulário de 261 tokens e não aloca listas de candidatos ou pesos a cada byte.
- Proteção de repetição: bloqueia o candidato que completaria uma repetição consecutiva idêntica de n-grama de 3 a 6 bytes. Se todos os candidatos forem bloqueados, a geração termina com `repetition_detected=true`.
- Limites de bytes, timeout e cancelamento continuam ativos.

## Gerenciamento de sessões

`SessionManager` mantém estado recorrente e RNG independentes por ID de sessão. Chamadas para o mesmo ID são serializadas; IDs diferentes podem gerar em paralelo. O limite padrão é 1.024 sessões e o TTL ocioso padrão é 30 minutos. Há operações para resetar, apagar, limpar e expirar sessões.

A primeira chamada reseta o estado. As chamadas seguintes retomam o estado anterior e incorporam o novo texto ao contexto recorrente. Se uma geração falhar ou for cancelada no meio do processamento, o estado parcial é descartado e a próxima chamada reinicia a sessão.

## Quantização INT8

O exportador converte seis matrizes para INT8 simétrico por linha, intervalo [-127,127], com uma escala FP32 por linha. Biases e parâmetros RMSNorm permanecem FP32. A saída reutiliza a matriz de embedding quantizada, preservando weight tying.

Layout G3 atual:
- 99.087 pesos de matrizes em INT8;
- 1.311 escalas FP32;
- 913 parâmetros auxiliares FP32;
- payload esperado: 107.983 bytes, contra 400.000 bytes de pesos FP32.

`G3Int8Backend` executa a GRU diretamente a partir dos pesos INT8; não desquantiza todas as matrizes para FP32 antes da inferência. As ativações e o estado recorrente permanecem FP32.

## Kernels

Os kernels FP32 foram ajustados para usar buffers `std::array` de tamanho estático para estados, gates, ativações, projeção e saída. O caminho por byte não cria vetores temporários no heap. As multiplicações percorrem matrizes row-major por linha para favorecer localidade e vetorização do compilador.

## Executável

`morok-g3-generate <modelo.morok> [opções] [prompt]` carrega artefatos FP32 ou INT8. Opções: `--temperature=0.7`, `--top-k=10`, `--max-output=512` e `--benchmark`. Sem prompt posicional, lê linhas de stdin mantendo uma sessão interativa até `/sair`.

Exemplo de benchmark: `morok-g3-generate modelo.morok --benchmark "Olá, tudo bem?"`.

## Medição

O benchmark executa o backend real, faz aquecimento e mede latência mediana e p95, bytes por segundo, bytes de estado por sessão, RSS antes/depois e pico de RSS quando disponível no Linux. RSS é memória residente do processo completo, não somente a Arena do modelo. Em plataformas sem contador de RSS, esses campos são zero; o tamanho do estado continua disponível.

Não há um número universal de latência/RAM gravado como resultado de CI: deve ser medido no hardware de destino e com o artefato treinado de produção.