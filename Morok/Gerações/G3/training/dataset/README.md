# Corpus inicial de produção — Morok G3 (pt-BR)

Versão: g3-ptbr-v1.0  
Arquivo: `g3_ptbr_v1.jsonl`  
Formato: JSONL UTF-8, um registro por intenção.

## Conteúdo

- 131 registros canônicos, cada um com `example_id` e `group_id` únicos.
- 262 paráfrases curadas (`question_variants`), duas por intenção.
- 393 perguntas textuais antes da perturbação dinâmica.
- Português brasileiro (`pt-BR`).
- Categorias: conversação, português, matemática, ciências, biologia, química, física, geografia, história, ambiente, economia básica, computação, programação, sistemas, redes, Git, engenharia de software, testes, dados, IA, segurança, privacidade, raciocínio, estudo, escrita, saúde geral e ética.
- Respostas de referência concisas, em linguagem direta.
- Nenhum registro contém dado pessoal intencional; todos usam `sensitive=false`.

## Expansão e ruído

As paráfrases foram escritas para preservar a intenção e compartilham o mesmo `group_id` do enunciado canônico. O loader G3 expande até quatro perguntas por registro, deduplica as perguntas e aplica perturbação apenas nos exemplos do split de treino.

A perturbação dinâmica padrão tem probabilidade de 45% por exemplo e cobre troca de caracteres adjacentes, omissão, substituição por tecla próxima, variação de acentos/caixa/pontuação e repetição/espaçamento. As mutações são feitas em caracteres Unicode e preservam UTF-8 válido. Não são gravadas como cópias estáticas no corpus.

## Split

O split padrão é feito por `group_id`, seed 42, proporções 85%/10%/5%. Com 131 grupos, o algoritmo atual aloca 111 grupos para treino, 13 para validação e 7 para teste. Nenhuma paráfrase de uma intenção atravessa os splits. A expansão e o ruído são aplicados somente ao treino.

## Validar e treinar

Na raiz do repositório:

```sh
cmake -S Morok -B Morok/build -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON -DMOROK_BUILD_APPS=ON
cmake --build Morok/build --target morok-g3-train morok_test_g3_training --parallel 2
ctest --test-dir Morok/build --output-on-failure -R g3.training
Morok/build/morok-g3-train Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl Morok/Gerações/G3/training/dataset/metrics-g3-ptbr-v1.jsonl Morok/Gerações/G3/training/dataset/g3-ptbr-v1.morok 10 42 --checkpoint=Morok/Gerações/G3/training/dataset/g3-ptbr-v1.g3ckpt
```

Para retomar uma execução interrompida entre épocas, use o mesmo dataset/configuração e:

```sh
Morok/build/morok-g3-train Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl Morok/Gerações/G3/training/dataset/metrics-g3-ptbr-v1.jsonl Morok/Gerações/G3/training/dataset/g3-ptbr-v1.morok 10 42 --resume=Morok/Gerações/G3/training/dataset/g3-ptbr-v1.g3ckpt
```

Após o SFT, avalie com a mesma seed:

```sh
Morok/build/morok-g3-evaluate Morok/Gerações/G3/training/dataset/g3_ptbr_v1.jsonl Morok/Gerações/G3/training/dataset/g3-ptbr-v1.morok Morok/Gerações/G3/training/dataset/evaluation-g3-ptbr-v1.json 42
```

Consulte também `../README.md` para os detalhes do pipeline e os limites do avaliador.

## Limites e status de qualidade

Este arquivo é um corpus inicial curado para validar e iniciar um primeiro ciclo SFT controlado. Ele não deve ser descrito como corpus universal, exaustivo ou suficiente para produzir um assistente geral robusto. São 131 intenções e 393 formas de pergunta antes da perturbação; isso é pequeno para estabelecer competência ampla em linguagem.

Antes de declarar um modelo de produção, amplie o corpus em profundidade e volume, acrescente diálogos multi-turno, tarefas de composição e transformação, perguntas com contexto, casos difíceis, contraexemplos, raciocínio verificável e um conjunto de teste independente revisado por humanos. Inclua fontes e licenças verificáveis caso dados externos sejam adicionados. Não misture dados de teste ao treino nem crie paráfrases de teste no treino.

O campo `source` registra curadoria interna; não representa validação externa independente. As respostas devem passar por revisão humana de domínio antes de uso em produção. O comando acima inicia SFT, não executa automaticamente o RL.
