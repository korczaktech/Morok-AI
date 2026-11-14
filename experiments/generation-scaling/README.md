# Escala e custo de geração

Esta pasta registra medições de inferência autoregressiva da G1. Não há campanha de escala concluída registrada aqui.

## Matriz sugerida

Separar as dimensões em campanhas controladas:

- Comprimento do prompt: curto, médio e próximo do limite permitido.
- Limite de geração: 1, 16, 64, 256 e 4096 bytes, respeitando os limites atuais da aplicação.
- Checkpoint: sempre registrar SHA-256 e época.
- Repetições: aquecimento separado e múltiplas medições por cenário.
- Ambiente: CPU, arquitetura, frequência quando disponível, sistema, RAM e limite do processo.

Não misturar tempos de carregamento do checkpoint com tempo de geração. Registrar ambos separadamente quando medidos.

## Métricas

- latência total e latência por byte gerado;
- bytes gerados por segundo;
- número de avaliações de candidatos, quando disponível;
- pico RSS do processo;
- bytes do prompt e bytes de saída;
- validade UTF-8 da saída;
- se o limite de bytes foi alcançado;
- status de saída e erros.

Para medições de latência, publicar mediana e percentis (p50/p95) com número de amostras. Não comparar execuções em máquinas diferentes como se fossem equivalentes.

## Execução

O executável `morok_generate` permite gerar a partir de checkpoint e prompt. Consulte `morok_generate --help` ou a documentação da aplicação para a sintaxe efetiva da versão testada. Cada campanha deve guardar o comando literal, hashes de checkpoint e prompt e ambiente.

A G1 atual usa decodificação greedy e limite de bytes. Não chamar throughput de tokens/s: a unidade atual é byte.
