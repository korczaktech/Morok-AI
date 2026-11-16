# Corpus pt-BR — Morok G3

Versão: g3-ptbr-v1.1  
Arquivo: `g3_ptbr_v1.jsonl`  
Formato: JSONL UTF-8, um registro canônico por intenção.

## Inventário

- 235 intenções com `example_id` e `group_id` únicos.
- 470 paráfrases curadas, duas por intenção.
- 705 formas de pergunta antes da perturbação dinâmica.
- 42 categorias semânticas.
- Idioma: português brasileiro (`pt-BR`).
- Todos os registros têm resposta de referência, proveniência interna e `sensitive=false`.
- Nenhum dado pessoal real foi intencionalmente incluído.

As categorias abrangem conversação, português, matemática, ciências, biologia, química, física, geografia, história, ambiente, economia básica, computação, programação, sistemas, redes, Git, engenharia de software, testes, dados, IA, segurança, privacidade, raciocínio, estudo, escrita, saúde geral, ética e tarefas cotidianas.

## Expansão e robustez

Cada intenção contém uma pergunta canônica e duas paráfrases. O carregador expande até quatro formas por registro; portanto, as três formas atualmente armazenadas são aproveitadas. A expansão é aplicada somente ao split de treino.

A perturbação dinâmica padrão tem probabilidade de 45% por exemplo e pesos condicionais:
- troca de caracteres adjacentes: 20%;
- omissão: 20%;
- substituição por tecla próxima: 20%;
- variação de acentos, caixa e pontuação: 25%;
- repetição e espaçamento: 15%.

Os pesos somam 100%. As mutações são feitas em caracteres Unicode e preservam UTF-8 válido. Validação e teste permanecem limpos.

## Split e prevenção de vazamento

A divisão padrão é feita por `group_id`, seed 42, nas proporções 85%/10%/5%. Com 235 grupos, a implementação atual produz 199 grupos de treino, 23 de validação e 13 de teste. Todas as formas de uma intenção permanecem no mesmo grupo e, consequentemente, no mesmo split.

A expansão e a perturbação são aplicadas somente ao treino. Validação orienta o acompanhamento do treinamento; teste fica reservado à avaliação final.

## Validar o corpus

Na raiz do repositório:

```sh
python3 Morok/Gerações/G3/training/dataset/validate_corpus.py Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl
```

O validador verifica UTF-8, JSONL, campos obrigatórios, versão, idioma, qualidade, ausência de registros sensíveis, duas paráfrases por intenção, IDs únicos, grupos únicos e perguntas duplicadas.

## Treinamento SFT

Compile os alvos necessários:

```sh
cmake -S Morok -B Morok/build -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON -DMOROK_BUILD_APPS=ON
cmake --build Morok/build --target morok-g3-train morok-g3-evaluate morok-g3-reinforce morok_test_g3_training --parallel 2
ctest --test-dir Morok/build --output-on-failure -R g3.training
```

Execute o SFT:

```sh
Morok/build/morok-g3-train Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl Morok/Gerações/G3/training/dataset/metrics-g3-ptbr-v1.jsonl Morok/Gerações/G3/training/dataset/g3-ptbr-v1-sft.morok 10 42 --checkpoint=Morok/Gerações/G3/training/dataset/g3-ptbr-v1.g3ckpt
```

Para retomar uma execução interrompida entre épocas, use a mesma configuração e:

```sh
Morok/build/morok-g3-train Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl Morok/Gerações/G3/training/dataset/metrics-g3-ptbr-v1.jsonl Morok/Gerações/G3/training/dataset/g3-ptbr-v1-sft.morok 10 42 --resume=Morok/Gerações/G3/training/dataset/g3-ptbr-v1.g3ckpt
```

## Avaliação e RL

Avalie o SFT com a mesma seed:

```sh
Morok/build/morok-g3-evaluate Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl Morok/Gerações/G3/training/dataset/g3-ptbr-v1-sft.morok Morok/Gerações/G3/training/dataset/evaluation-g3-ptbr-v1.json 42
```

Depois da inspeção do relatório SFT, execute o RL:

```sh
Morok/build/morok-g3-reinforce Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl Morok/Gerações/G3/training/dataset/g3-ptbr-v1-sft.morok Morok/Gerações/G3/training/dataset/g3-ptbr-v1-rl.morok Morok/Gerações/G3/training/dataset/rl-report-g3-ptbr-v1.json 3
```

O comando RL usa o SFT como política de referência congelada, atualiza uma cópia treinável e compara as políticas no split de teste. O avaliador automático é lexical e não substitui revisão humana.

## Limites de qualidade

A versão 1.1 é um corpus inicial ampliado para iniciar ciclos controlados de SFT e RL. Ela não é um corpus universal nem prova competência geral. As respostas foram preparadas por curadoria interna e ainda precisam de revisão humana independente, especialmente em domínios factuais e técnicos.

Para uma geração de produção, amplie a cobertura com diálogos multi-turno, tarefas de composição e transformação, contexto longo, exemplos adversariais, raciocínio verificável, contraexemplos e teste independente revisado. Não use dados de teste para criar paráfrases de treino. Se dados externos forem adicionados, registre fonte, licença e versão.
