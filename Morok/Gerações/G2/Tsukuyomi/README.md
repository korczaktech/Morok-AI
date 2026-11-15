# Tsukuyomi — G2

**Geração:** Segunda Geração (G2)  
**Modelo:** Tsukuyomi  
**Versão:** 0.1.0  
**Perfil:** modelo mais avançado da linha G2  
**Orçamento:** exatamente 10.000 parâmetros treináveis

O Tsukuyomi prioriza capacidade e tarefas complexas. A versão 0.1 entrega um modelo neural autoregressivo byte-level, com embeddings compartilhados, contexto causal, treinamento Adam, métricas train_bpb, geração greedy com transições UTF-8 seguras e checkpoints versionados. A implementação é privada da G2 e não importa código da G1.

## Estrutura

- include/morok/g2/tsukuyomi/model.hpp: contrato público do modelo.
- src/model.cpp: parâmetros, inicialização, forward pass, avaliação, gradientes, Adam, geração e checkpoints.
- apps/train.cpp: treinamento de corpus local, com opção de continuar a partir de checkpoint.
- apps/generate.cpp: inferência a partir de checkpoint.
- scripts/treinamento_ciclico.py: ciclos de treinamento e geração para todas as perguntas.
- tests/test_model.cpp: invariantes, aprendizado, geração e persistência.
- ARCHITECTURE.md: especificação matemática, inventário e limitações.

## Compilação

A partir de Morok/:

cmake -S . -B build/g2 -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON -DMOROK_BUILD_APPS=ON

cmake --build build/g2 --target morok_g2_tsukuyomi morok_g2_tsukuyomi_train morok_g2_tsukuyomi_generate morok_test_g2_tsukuyomi --parallel 2

ctest --test-dir build/g2 -R g2.tsukuyomi.model --output-on-failure

## Treinamento e inferência

build/g2/morok-g2-tsukuyomi-train corpus.bin tsukuyomi-g2-v0.1.ckpt 10 42

Para continuar o treinamento usando os parâmetros de um checkpoint existente:

build/g2/morok-g2-tsukuyomi-train corpus.bin tsukuyomi-g2-v0.1.ckpt 10 42 --resume

build/g2/morok-g2-tsukuyomi-generate tsukuyomi-g2-v0.1.ckpt "Olá" 120

O corpus é um arquivo binário de 2 bytes a 512 MiB. O modelo não inclui corpus de treinamento.

## Treinamento cíclico pelo GitHub Actions

O workflow manual **Treinamento — G2** fica disponível na aba Actions. Ele oferece:

- Checkbox para habilitar o Tsukuyomi.
- **Épocas:** passagens completas pelo corpus em cada ciclo.
- **Tentativas:** quantidade de ciclos quando o treinamento contínuo estiver desligado. Em cada ciclo, o modelo treina pelas épocas configuradas usando somente os bytes das respostas IA: e avalia as quatro perguntas do conjunto de validação.
- **Treinamento contínuo:** checkbox opcional. Quando ativada, ignora o limite fixo de tentativas e continua até obter 10 ciclos consecutivos em que todas as perguntas atinjam o critério de aprovação. Um ciclo com qualquer resposta reprovada zera a sequência.

O primeiro ciclo inicializa o modelo. Os ciclos seguintes carregam os parâmetros e o estado completo do Adam (primeiro e segundo momentos e contador de passos), evitando reiniciar o otimizador. O gerador é iniciado uma vez por ciclo e carrega o checkpoint uma única vez. A validação usa quatro perguntas separadas do treinamento; o teste reservado contém outras quatro perguntas e só é consultado uma vez, após o treinamento. Cada resposta termina no primeiro fim de linha gerado ou ao atingir 160 bytes. O relatório mostra respostas, gabaritos, correspondência lexical e resultado por pergunta; os arquivos do modelo, do Adam e do relatório ficam disponíveis como artefatos.

O avaliador atual é determinístico e compara palavras normalizadas preservando a frequência de cada palavra (F1 de pelo menos 90%, ou igualdade após normalização). Isso permite uma verificação automática reproduzível, mas não equivale a compreensão semântica e pode rejeitar paráfrases corretas ou aceitar coincidências lexicais. A aprovação deve ser interpretada como correspondência ao gabarito, não como prova de inteligência ou entendimento.

O treinamento acumula gradientes em mini-lotes de 16 bytes-alvo por padrão, reduzindo as atualizações do Adam. O estado do Adam é salvo em arquivo separado com versão e checksum, junto do checkpoint. A retomada exige os dois arquivos válidos. O workflow tem limite operacional de seis horas, portanto uma execução contínua pode ser interrompida pelo limite do GitHub Actions antes de atingir dez ciclos perfeitos.

## Escopo e limitações

A versão 0.1 continua sendo um modelo byte-level com contexto finito de 14 bytes; a identidade e o orçamento de 10.000 parâmetros não foram alterados. O treinador agora aplica uma máscara de alvos em corpora U:/IA: e calcula a perda somente sobre os bytes das respostas IA:, mantendo o contexto anterior disponível. Isso torna o objetivo mais direcionado à resposta, mas não resolve a limitação de contexto: o modelo ainda não consegue reter a pergunta inteira quando ela excede a janela efetiva.

Ainda não há recorrência, BPTT, atenção, memória externa ou Reinforcement Learning (RL). A avaliação lexical não fornece recompensa ao modelo nem atualiza os pesos. A geração preserva UTF-8 válido para prompts válidos, mas isso não comprova qualidade linguística ou compreensão. Consulte ARCHITECTURE.md.

O orçamento é conferido estaticamente e representa parâmetros implementados, não qualidade ou desempenho comprovados.

## Corpus conversacional inicial

O diretório data/ mantém treinamento-inicial.txt como corpus original de referência, com 40 pares de diálogo em português e 4.439 bytes em UTF-8. Para o workflow, os diálogos foram separados deterministicamente em treinamento.txt (32 pares), validacao.txt (4 pares) e teste.txt (4 pares), sem perguntas repetidas entre os conjuntos. A validação acompanha os ciclos; o teste reservado só é executado ao final.

Para treinar usando esse material, a partir de Morok/:

build/g2/morok-g2-tsukuyomi-train "Gerações/G2/Tsukuyomi/data/treinamento.txt" build/g2/tsukuyomi-inicial.ckpt 10 42

O arquivo é um corpus de modelagem de linguagem, não um conjunto de pares supervisionados com rótulos separados. O modelo aprende a prever a sequência inteira de bytes, incluindo os prefixos U: e IA:; isso não garante que ele aprenda a seguir o formato de diálogo ou responder corretamente a cada pergunta. A qualidade deve ser medida em exemplos que não estejam no corpus.
