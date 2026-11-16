# Morok G3 — dataset e treinamento supervisionado

Este módulo implementa o pipeline SFT da G3 sobre o backend neural existente. Não cria Core, runtime de inferência ou cópia do motor. A biblioteca é `Morok::G3::Training`; o executável é `morok-g3-train`.

## Schema JSONL

Um exemplo por linha. Campos obrigatórios:

- `example_id`: identificador único no dataset;
- `group_id`: família de intenção; paráfrases e variantes da mesma família usam o mesmo valor;
- `category`: categoria da intenção;
- `question`: pergunta original;
- `answer`: resposta de referência;
- `source`: proveniência/origem do rótulo;
- `language`: idioma;
- `dataset_version`: versão do conjunto.

Campos opcionais:

- `quality`: número entre 0 e 1 (default 1);
- `sensitive`: booleano (default false; sinalização para política externa de tratamento);
- `question_variants`: lista de paráfrases curadas da pergunta;
- `expansion_axes`: matriz de opções para os espaços `{0}`, `{1}` etc. no texto da pergunta. Cada eixo representa alternativas para um fragmento; os eixos podem codificar sinônimos, ordem ou formalidade.

Exemplo:

```json
{"example_id":"pt-0001","group_id":"cumprimento-01","category":"cumprimento","question":"Olá, tudo bem?","answer":"Olá! Como posso ajudar?","source":"curadoria-interna","language":"pt-BR","dataset_version":"g3-v1","quality":1.0,"sensitive":false,"question_variants":["Oi, tudo bem?","Como você está?"]}
```

Os textos são validados como UTF-8 e normalizados em NFC via ICU. IDs duplicados, campos obrigatórios ausentes, valores inválidos e datasets vazios são rejeitados. Exemplos marcados como `sensitive=true` bloqueiam o treinamento por padrão. Para utilizá-los, a aplicação deve habilitar explicitamente `TrainingConfig::allow_sensitive_examples`; isso não substitui base legal, minimização, anonimização ou política de retenção.

## Separação contra vazamento

O split padrão é 85% treino, 10% validação e 5% teste, aplicado a grupos completos por `group_id`. O algoritmo exige pelo menos três grupos distintos e mantém cada grupo integralmente em um único split. A expansão e as perturbações são executadas somente nos exemplos do split de treino. Validação permanece limpa; o conjunto de teste é mantido separado e não é usado para ajuste de hiperparâmetros.

## Expansão e robustez

A expansão usa variantes curadas em `question_variants` e eixos combinatórios em `expansion_axes`. Para cada placeholder `{i}`, o eixo i fornece alternativas de fragmento. O algoritmo mantém uma combinação canônica, amostra combinações adicionais com seed determinística, deduplica e respeita o limite total por exemplo (default 4). Sinônimos, ordem e formalidade são fornecidos pelo dataset, sem inventar equivalências semânticas. Depois da expansão, cada candidato de treino pode receber perturbação.

Defaults experimentais da perturbação:
- 45% de probabilidade por exemplo;
- swap adjacente: 20% das operações;
- omissão: 20%;
- substituição por tecla próxima: 20%;
- acento/caixa: 25%;
- repetição/espaçamento: 15%.

As mutações são feitas em caracteres Unicode e preservam UTF-8 válido. Não são aplicadas a validação ou teste. Os pesos condicionais precisam somar exatamente 100%.

## Objetivo e BPTT

A sequência supervisionada é formada por BOS, USR, bytes da pergunta, BOT, bytes da resposta e EOS. O objetivo padrão é causal e calcula loss somente nos bytes da resposta e em EOS; tokens de prompt não recebem loss auxiliar. PAD é ignorado. Label smoothing default 0,05 é aplicado somente quando o alvo é byte comum (0–255); tokens especiais não recebem smoothing.

BPTT truncado usa segmentos de 128 passos por padrão. O estado numérico é transportado entre segmentos, mas o gradiente é interrompido na fronteira (detach), conforme TBPTT. A máscara de dropout variacional é fixa dentro de cada segmento e desativada na validação.

## AdamW e regularização

Defaults iniciais:
- learning rate: 1e-3;
- warmup: 3% dos updates planejados;
- cosine decay até 1e-5;
- AdamW beta1=0,9, beta2=0,999, epsilon=1e-8;
- weight decay 1e-3 apenas em matrizes de peso;
- dropout variacional 0,10;
- clipping global L2 em 1,0.

Biases e escala RMSNorm não recebem weight decay. O gradiente é calculado por BPTT e atualizado com AdamW; o contador continua em exatamente 100.000 parâmetros.

## Loss e telemetria

O arquivo JSONL de métricas recebe eventos append-only:
- `example_loss`: run_id, epoch, example_id, group_id, question_loss, example_loss, norma de gradiente antes/depois do clipping;
- `epoch`: loss média de treino, loss média de validação, contagens, updates, seed e norma de gradiente.

A loss por pergunta e por exemplo é mantida individualmente; não é sobrescrita por uma média global. Métricas são associadas ao run_id, fingerprint determinístico do dataset e seed.

## Uso

Compile com CMake e execute:

```sh
cmake -S Morok -B Morok/build -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON -DMOROK_BUILD_APPS=ON
cmake --build Morok/build --target morok-g3-train morok_test_g3_training --parallel 2
Morok/build/morok-g3-train caminho/dataset.jsonl caminho/metrics.jsonl caminho/modelo.morok 10 42
```

O executável inicia um treino novo, escreve métricas JSONL e exporta os 100.000 pesos FP32 finais para um artefato `.morok` versionado, validado por CRC32 e gravado atomicamente. Não foi incluído um corpus de produção no repositório. A qualidade do modelo não pode ser inferida dos testes sintéticos de CI; o treinamento real depende de um dataset com proveniência e rótulos revisados.

## Limites conhecidos desta entrega

- As alternativas semânticas precisam ser fornecidas e revisadas em `question_variants` ou `expansion_axes`; não existe um gerador de sinônimos baseado em léxico externo.
- O campo `sensitive` é registrado, mas a anonimização/controle de acesso ao corpus é responsabilidade do pipeline de ingestão e armazenamento.
- A implementação registra fingerprint FNV-1a de 64 bits para identificação/reprodutibilidade local; não é hash criptográfico de integridade.
- O CLI exporta o modelo final, mas ainda não retoma uma execução interrompida; checkpoint recuperável precisa persistir também momentos AdamW, RNG, cursor/sampler e versão do dataset.
