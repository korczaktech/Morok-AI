# Morok G3 — dataset e treinamento supervisionado

Este módulo implementa o pipeline SFT da G3 sobre o backend neural existente. Não cria Core, runtime de inferência ou cópia do motor. A biblioteca é `Morok::G3::Training`; o executável é `morok-g3-train`.

## Corpus oficial de desenvolvimento

O corpus pt-BR versionado está em `dataset/g3_ptbr_v1.jsonl` e é descrito em `dataset/README.md`. A versão atual contém 235 intenções, 470 paráfrases curadas e 705 formas de pergunta em 42 categorias. É adequado para iniciar um ciclo experimental controlado e testar o pipeline, mas não deve ser considerado exaustivo ou suficiente para declarar competência geral. Dos 235 registros, 104 são expansão assistida sinalizada para revisão humana (`review_status=pending-human-review`, `quality=0.80`); não os trate como rótulos revisados.

Valide o arquivo antes de treinar:

```sh
python3 Morok/Gerações/G3/training/dataset/validate_corpus.py Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl
```

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
- `sensitive`: booleano (default false; registros sensíveis são bloqueados por padrão);
- `question_variants`: lista de paráfrases curadas;
- `expansion_axes`: matriz de opções para espaços `{0}`, `{1}` etc. no texto da pergunta.

Os textos são validados como UTF-8 e normalizados em NFC via ICU. IDs duplicados, campos obrigatórios ausentes, valores inválidos e datasets vazios são rejeitados.

## Separação contra vazamento

O split padrão é 85% treino, 10% validação e 5% teste, aplicado a grupos completos por `group_id`. A expansão e as perturbações são executadas somente nos exemplos do split de treino. Validação permanece limpa; teste é mantido separado e não é usado para ajuste de hiperparâmetros.

## Expansão e robustez

A expansão usa variantes curadas em `question_variants` e eixos combinatórios em `expansion_axes`. O algoritmo mantém a pergunta canônica, amostra combinações adicionais com seed determinística, deduplica e respeita o limite de quatro formas por exemplo. Sinônimos, ordem e formalidade precisam ser fornecidos no dataset; o sistema não inventa equivalências semânticas.

A perturbação padrão tem probabilidade de 45% por exemplo e pesos condicionais de 20% swap, 20% drop, 20% tecla próxima, 25% acentos/caixa/pontuação e 15% repetição/espaçamento. As mutações são feitas em caracteres Unicode e preservam UTF-8 válido. Não são aplicadas a validação ou teste.

## Prompt mestre

O SFT e o RL usam o mesmo prompt de sistema, definido em
`include/morok/g3/training/system_prompt.hpp`:

```text
<|system|>Morok: responda em português, com clareza; não invente e admita dúvidas.</|system>
```

O texto foi mantido curto para limitar o custo de contexto de uma arquitetura de
100.000 parâmetros. As tags são bytes UTF-8 comuns; nenhum token especial ou
parâmetro novo foi adicionado.

## Objetivo e otimização

A sequência supervisionada é formada por BOS, pelo prompt mestre em bytes UTF-8 delimitado por `<|system|>` e `</|system|>`, USR, bytes da pergunta, BOT, bytes da resposta e EOS. O prompt mestre também é inserido no contexto de cada pergunta do RL independente. As tags de sistema são texto comum byte-level, não novos IDs de vocabulário: a arquitetura permanece com 261 IDs e exatamente 100.000 parâmetros. A versão do prompt integra o fingerprint de configuração, impedindo retomada de checkpoints criados com outro contrato. O objetivo calcula loss nos bytes da resposta e em EOS; tokens de contexto não recebem loss auxiliar e PAD é ignorado.

Defaults atuais:
- BPTT truncado de 128 passos;
- Cross-Entropy causal com label smoothing 0,05 nos bytes comuns;
- AdamW: beta1=0,9, beta2=0,999 e epsilon=1e-8;
- learning rate inicial 1e-3, warmup de 3% e cosine decay até 1e-5;
- weight decay 1e-3 apenas em matrizes de peso;
- dropout variacional 0,10;
- clipping global L2 em 1,0.

Biases e escala RMSNorm não recebem weight decay. Esses são os defaults implementados no pipeline G3 atual; não confundir com parâmetros de propostas arquiteturais anteriores.

## Checkpoints e telemetria

O treinamento registra loss por exemplo e por época, contagens, updates, seed e norma de gradiente. Checkpoints guardam pesos FP32, momentos AdamW, scheduler, sampler, RNG, fingerprints e configuração. A retomada é validada e ocorre no limite entre épocas.

O fingerprint FNV-1a é usado para identificação/reprodutibilidade local, não como hash criptográfico.

## Execução

Na raiz do repositório:

```sh
cmake -S Morok -B Morok/build -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON -DMOROK_BUILD_APPS=ON
cmake --build Morok/build --target morok_g3_train morok_g3_evaluate morok_g3_reinforce morok_test_g3_training --parallel 2
ctest --test-dir Morok/build --output-on-failure -R g3.training
```

SFT:

```sh
Morok/build/morok-g3-train Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl Morok/Gerações/G3/training/dataset/metrics-g3-ptbr-v1.jsonl Morok/Gerações/G3/training/dataset/g3-ptbr-v1-sft.morok 10 42 --checkpoint=Morok/Gerações/G3/training/dataset/g3-ptbr-v1.g3ckpt
```

Avaliação independente do SFT:

```sh
Morok/build/morok-g3-evaluate Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl Morok/Gerações/G3/training/dataset/g3-ptbr-v1-sft.morok Morok/Gerações/G3/training/dataset/evaluation-g3-ptbr-v1.json 42
```

RL independente (não requer executar SFT):

```sh
Morok/build/morok-g3-reinforce Morok/Gerações/G3/rl/dataset/g3_rl_ptbr_v1.jsonl Morok/Gerações/G3/training/dataset/g3-ptbr-v1-rl.morok Morok/Gerações/G3/training/dataset/rl-report-g3-ptbr-v1.json 3 42
```

O RL inicializa uma política própria a partir da seed, usa quatro candidatas por prompt, baseline móvel por categoria, retorno descontado e avaliador de referência lexical. O fluxo RL exclui exemplos sensíveis e registros com `quality<1.0`, evitando usar exemplos pendentes de revisão humana como alvo de recompensa. A penalidade KL contra uma política congelada é opcional e fica desativada por padrão; o CLI independente não carrega modelo SFT. O avaliador não garante verdade factual, segurança ou qualidade semântica; use revisão humana antes de tratar o resultado como modelo de produção.

## Limitações conhecidas

- As paráfrases são curadas manualmente; não há gerador semântico externo.
- A cobertura atual não representa todos os assuntos, estilos, diálogos ou tarefas que um assistente geral pode receber.
- A rubrica RL baseada em referências pode favorecer semelhança lexical e precisa de auditoria para evitar reward hacking.
- A qualidade linguística e factual depende de revisão independente; testes sintéticos de CI não comprovam qualidade do modelo.


## Analisador automático de respostas

O executável `morok-g3-analyze` executa inferência real com o artefato G3 e compara cada resposta gerada com a resposta de referência do corpus. Para cada intenção, analisa a pergunta canônica e suas duas paráfrases: 235 intenções × 3 formas = 705 tentativas.

A métrica `Nivel lexical` é similaridade textual normalizada por distância de edição em bytes UTF-8, apresentada de 0% a 100%. O status `CORRETO` é atribuído quando a similaridade alcança o limiar configurado; caso contrário, aparece `INCORRETO`. O padrão do workflow é 80%, configurável pelo input `limiar_lexical` ao iniciar a execução.

O analisador usa decodificação gulosa (temperatura 0 e Top-K 1), gera uma resposta por pergunta e escreve uma linha verde para sucesso ou vermelha para falha nos logs do GitHub Actions, no formato:

```text
Sucesso | Pergunta 001/705 | CORRETO | Nivel lexical: 92.4%
```

Cada tentativa também é registrada em `answer-analysis.jsonl`, com pergunta, resposta de referência, resposta gerada, nível lexical, limiar e classificação. O relatório é salvo junto ao artefato do ciclo SFT ou da execução RL.

**Limite:** similaridade lexical é uma verificação automática de proximidade textual, não uma prova de equivalência semântica ou factual. Respostas corretas formuladas com palavras diferentes podem receber pontuação baixa; respostas lexicalmente parecidas também podem conter erros de significado. Os resultados devem ser tratados como sinal de triagem, não como avaliação humana definitiva.
