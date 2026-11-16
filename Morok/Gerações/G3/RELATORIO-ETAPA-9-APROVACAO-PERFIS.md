# G3 — Etapa 9: aprovação e perfis

Data da validação: 2026-11-16
Branch: main
Arquitetura G3: 0x0302
Parâmetros: exatamente 100.000
Estado recorrente FP32: 660 bytes por sessão

## Escopo aprovado

A Etapa 9 fecha a validação automatizada das etapas anteriores e disponibiliza
os quatro perfis sobre a mesma execução. O motor Morok e o contrato
`morok::model::ModelBackend` permanecem únicos. Nenhum Core paralelo foi criado.

## Testes automatizados

A suíte G3 cobre:

- contagem exata de parâmetros, inicialização e contrato neural;
- inferência FP32 e INT8, artefatos, corrupção e checksum;
- temperatura, Top-K, tokens de controle, UTF-8, repetição e limites;
- timeout, cancelamento, isolamento concorrente e ciclo de vida de sessões;
- dataset, split por grupo, perturbação, loss causal, BPTT e AdamW;
- serialização/validação de checkpoint, checksum, gravação atômica e restauração;
- comparação determinística entre treinamento ininterrupto e treinamento
  interrompido após uma época, retomado do checkpoint, incluindo igualdade
  exata dos pesos finais;
- pipeline SFT → RL → comparação em conjunto de teste;
- exportação FP32 → INT8 → inferência real;
- políticas dos quatro perfis e isolamento das sessões dos agentes.

A retomada SFT é suportada em fronteiras entre épocas. Uma interrupção dentro
de uma época retorna ao último checkpoint íntegro e repete essa época; não há
retomada a partir de um lote intermediário. A configuração total de épocas,
dataset, arquitetura, versão de código e hiperparâmetros precisa corresponder.

## Perfis sobre a mesma base

Todos os perfis carregam o mesmo backend, artefato e runtime G3 (100.000
parâmetros). Não possuem pesos próprios.

| Perfil | Política validada | Agentes |
|---|---|---:|
| Mokosh | baixo consumo por limites menores de contexto/saída, Top-K 4 e capacidade recomendada de 8 sessões | 1 |
| Tsukuyomi | identidade escura como metadado de apresentação; exatamente um agente | 1 |
| Artemis | três agentes com sessões e RNG independentes; respostas separadas | 3 |
| Apollo | temperatura zero (greedy), Top-K 1 e limites de baixa latência | 1 |

Artemis ainda não possui votação, planejamento ou síntese automática. Executa
três gerações independentes e retorna os resultados separadamente. O tema
escuro de Tsukuyomi é metadado para a camada de apresentação; o CLI textual não
renderiza interface visual.

## Comparação entre gerações

| Geração/modelo | Parâmetros por modelo | Arquitetura/observação |
|---|---:|---|
| G1 | 1.000 | duas escalas recorrentes compactas e cabeça byte-level |
| G2 Tsukuyomi | 10.000 | modelo byte-level; projeto mantém até 100 checkpoints independentes |
| G3 | 100.000 | GRU de duas camadas, RMSNorm, projeção e weight tying; FP32/INT8 |

A G3 tem 100 vezes o orçamento de parâmetros de um modelo G1 e 10 vezes o de
um Tsukuyomi G2 individual. O conjunto de checkpoints independentes da G2 não
é diretamente equivalente a um único modelo G3.

A comparação acima é arquitetural e de orçamento. Não é uma comparação de
qualidade ou velocidade: G1, G2 e G3 usam arquiteturas, pipelines, objetivos e
interfaces diferentes. O workflow G2 não mantém um conjunto holdout ativo para
as 100 perguntas do Tsukuyomi; portanto, não há base para comparar loss de teste
entre G2 e G3. A comparação de qualidade só será válida após executar os
modelos com o mesmo corpus independente, divisão por grupo, prompts, hardware,
compilação, número de repetições e protocolo de avaliação.

## Benchmark e memória

O teste de implantação gera um artefato FP32 sintético, converte-o para INT8,
carrega o backend INT8 e executa benchmark com aquecimento e 30 medições.
O relatório JSON da quantização e a medição de inferência são publicados como
artefato da GitHub Actions (`g3-stage9-benchmark`).

Métricas emitidas:
- tamanho do payload FP32/INT8 e quantidade de escalas;
- RSS antes/depois e pico de RSS do processo;
- estado por sessão;
- latência mediana e P95;
- throughput em bytes por segundo.

Medição real da CI (Ubuntu 24.04, artefato de teste sintético, 30 execuções,
prompt "Morok G3", saída limitada a 8 bytes):

| Métrica | Resultado |
|---|---:|
| Payload FP32 | 400.000 bytes |
| Payload INT8 | 107.983 bytes |
| Escalas FP32 | 1.311 |
| Estado por sessão | 660 bytes |
| RSS antes da geração | 4.931.584 bytes |
| RSS depois da geração | 5.672.960 bytes |
| Pico RSS reportado | 5.672.960 bytes |
| Latência mediana | 829.242 ns (0,829 ms) |
| Latência P95 | 840.286 ns (0,840 ms) |
| Throughput | 8.746,47 bytes/s |

O payload INT8 é 72,999% menor que o payload FP32. O RSS é medição do processo
inteiro, não somente dos pesos; varia com sistema, alocador, bibliotecas e
runner. A medição de latência é para geração curta, sobre pesos de um treino
sintético de smoke test; não representa desempenho de produção ou de conversas
longas. Os números caracterizam o runner Linux da CI e não devem ser tratados
como benchmark de hardware de produção. O artefato JSON com a medição está
publicado na execução [G3 Stage 9 benchmark](https://github.com/korczaktech/Morok-AI/actions/runs/43924662542).

## Limitações e resultados

- A CI usa fixtures sintéticas; nenhum resultado de qualidade linguística de
  produção foi estabelecido nesta etapa.
- O avaliador RL usa referências e sinais lexicais; não substitui julgamento
  semântico humano nem garante factualidade.
- O fingerprint do dataset/configuração é FNV-1a para identificação local, não
  assinatura criptográfica.
- Mokosh usa Top-K 4; em byte-level, isso pode excluir bytes raros necessários
  a certos caracteres Unicode e deve ser avaliado em corpus real.
- Os quatro perfis compartilham os mesmos pesos; não há checkpoints separados.
- Artemis não agrega as respostas dos agentes.
- Os benchmarks devem ser repetidos no hardware de destino antes de prometer
  metas de latência ou RAM.

## Execução

A partir do diretório `Morok/`:

```sh
cmake -S . -B build/g3-approval -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON -DMOROK_BUILD_APPS=ON
cmake --build build/g3-approval --parallel 2
ctest --test-dir build/g3-approval -R '^g3\.' --output-on-failure
```

Treinamento com checkpoint:

```sh
morok-g3-train dataset.jsonl metrics.jsonl modelo.morok 10 42 --checkpoint=treino.g3ckpt --max-epochs-this-run=3
morok-g3-train dataset.jsonl metrics.jsonl modelo.morok 10 42 --resume=treino.g3ckpt
```

Perfis:

```sh
morok-g3-generate modelo.morok --profile=mokosh "pergunta"
morok-g3-generate modelo.morok --profile=tsukuyomi "pergunta"
morok-g3-generate modelo.morok --profile=artemis "pergunta"
morok-g3-generate modelo.morok --profile=apollo "pergunta"
```

## Status de aprovação

<badge>APROVADA</badge>

- Commit de código validado: `9534d1d50f959878630e24245736d92c8812a77f`.
- Workflow G3: [43924662542](https://github.com/korczaktech/Morok-AI/actions/runs/43924662542) — sucesso.
- Testes G3: 12/12 aprovados, 0 falhas, 1,05 s.
- CI geral: [43924662643](https://github.com/korczaktech/Morok-AI/actions/runs/43924662643) — sucesso nos quatro ambientes: GCC Release, GCC Debug, Clang Release e Clang Debug.
- Artefato de benchmark: `g3-stage9-benchmark`, publicado na execução G3 acima.

A aprovação é de implementação, compatibilidade e regressão automatizada. Não
é certificação de qualidade semântica do modelo: isso depende de treinamento
com corpus de produção e avaliação independente.
