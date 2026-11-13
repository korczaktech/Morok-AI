# Etapa 11 — Geração autoregressiva da G1

## Objetivo

Disponibilizar inferência independente do treinamento: carregar um checkpoint de inferência, consumir um prompt UTF-8 como contexto, prever bytes autoregressivamente e gravar uma continuação UTF-8 válida, sem pesos pré-treinados ou serviços externos.

## Implementação

- API pública em `include/morok/model/learn/g1_generation.hpp`.
- Implementação em `src/model/learn/g1_generation.cpp`.
- CLI `morok_generate` em `apps/generate_g1.cpp`.
- Testes em `tests/unit/model/test_g1_generation.cpp`.
- Checkpoint de inferência MOROKCKP v1 carregado por `load_g1_checkpoint`; o arquivo deve conter os 1.000 parâmetros e pode omitir o estado do otimizador.
- Prompt é limitado a 64 KiB na CLI e validado por `Utf8Text` estrito antes de qualquer inferência.
- Estado fast/slow é zerado no início de cada chamada e o prompt é consumido em ordem causal.
- Decodificação gulosa: em cada posição são avaliados os bytes candidatos permitidos pela máquina de estados UTF-8; escolhe-se o de menor NLL condicional. Empates mantêm o menor valor de byte pela ordem de varredura.
- Restrições UTF-8 cobrem bytes iniciais, continuações, overlong, surrogates e limite U+10FFFF. A seleção também impede iniciar uma sequência multibyte que não possa terminar dentro do orçamento restante.
- Limite de saída de 1 a 65.536 bytes; padrão da CLI: 256.
- A saída contém somente a continuação, não repete o prompt.
- A geração é determinística para mesmos parâmetros, prompt e limite.

## Uso

Treine e exporte o melhor checkpoint selecionado pela validação:

```sh
./build/morok_train ./data/corpus 10 42 \
  --checkpoint ./artifacts/g1-training.ckpt \
  --export-best ./artifacts/g1-best.ckpt
```

Crie um arquivo UTF-8 com o prompt e gere até 256 bytes:

```sh
./build/morok_generate ./artifacts/g1-best.ckpt \
  ./prompt.txt ./continuacao.txt
```

Defina outro limite em bytes:

```sh
./build/morok_generate ./artifacts/g1-best.ckpt \
  ./prompt.txt ./continuacao.txt 512
```

A CLI grava JSON resumido no stdout com bytes gerados, quantidade de avaliações de candidatos e indicação de limite atingido. Erros de arquivo, checkpoint, limite ou UTF-8 retornam código diferente de zero.

## Contrato de geração

1. O prompt completo é contexto observado; não é retornado na continuação.
2. O estado recorrente é atualizado somente com bytes já observados ou gerados.
3. A avaliação do byte candidato não altera o estado recorrente.
4. O byte escolhido é então consumido uma única vez pelo estado.
5. A continuação sempre termina em fronteira válida UTF-8.
6. A G1 não possui token EOS. A parada é pelo limite de bytes ou por ausência de candidato admissível.
7. Não há amostragem, temperatura, top-k, top-p, repetição penalizada nem streaming nesta etapa; o decodificador é greedy e determinístico.

## Testes

- Prompt vazio e prompt UTF-8 multibyte.
- Determinismo da decodificação.
- Limite máximo de bytes.
- Validade UTF-8 estrita da continuação.
- Prompt multibyte com orçamento de saída curto; sequência gerada continua UTF-8 válida.
- Rejeição de limite zero.
- Integração com checkpoint de inferência sem estado do otimizador.
- Causalidade: score de candidato não altera estado; byte escolhido é consumido após a decisão.

## Limitações e critérios não alegados

A geração executa a cabeça atual de 1.000 parâmetros. A saída pode ser incoerente, repetitiva ou conter controles UTF-8 válidos sem utilidade linguística. A restrição de UTF-8 garante codificação, não qualidade semântica. A G1 não tem EOS, modelo conversacional, memória de diálogo ou amostragem nesta etapa.

A avaliação de qualidade continua dependente de corpus real/licenciado, múltiplas sementes e hardware de referência. O sucesso dos testes demonstra correção mecânica do decodificador, não competência linguística.
