# Tsukuyomi — G2

**Geração:** Segunda Geração (G2)  
**Modelo:** Tsukuyomi  
**Versão:** 0.1.0  
**Perfil:** modelo mais avançado da linha G2  
**Orçamento:** exatamente 10.000 parâmetros treináveis

O Tsukuyomi prioriza capacidade e tarefas complexas. A versão 0.1 entrega um modelo neural autoregressivo byte-level com dois canais linguísticos auxiliares aprendidos (categoria e fronteira), compartilhados com os embeddings de bytes, e célula recorrente com retenção temporal aprendida e estado oculto 32, matriz recorrente compartilhada, contexto inicial da pergunta em 13 regiões posicionais contíguas e BPTT truncado em até 64 etapas. A porta sigmoide de retenção combina estado anterior e candidato tanh sem adicionar parâmetros; o forward mantém o estado ao longo de todo o histórico da resposta, embora o gradiente continue truncado. Usa embeddings compartilhados, Adam, métricas train_bpb, geração greedy com transições UTF-8 seguras e checkpoints versionados. A implementação é privada da G2 e não importa código da G1.

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
Na inferência, as 13 regiões posicionais contíguas da pergunta são preparadas uma única vez e usadas para inicializar o estado recorrente. O estado oculto de 32 dimensões é atualizado a cada byte gerado e mantido entre os passos, sem exceder os 10.000 parâmetros.


build/g2/morok-g2-tsukuyomi-train corpus.bin tsukuyomi-g2-v0.1.ckpt 10 42

Para continuar o treinamento usando os parâmetros de um checkpoint existente:

build/g2/morok-g2-tsukuyomi-train corpus.bin tsukuyomi-g2-v0.1.ckpt 10 42 --resume

build/g2/morok-g2-tsukuyomi-generate tsukuyomi-g2-v0.1.ckpt "Olá" 120

O corpus é um arquivo binário de 2 bytes a 512 MiB. O modelo não inclui corpus de treinamento.

## Treinamento cíclico pelo GitHub Actions

O workflow manual **Treinamento — G2** fica disponível na aba Actions. O padrão executa até 1.000 épocas por tentativa e até 100 tentativas/ciclos (máximo teórico de 100.000 épocas por execução), seleciona até 20 perguntas de validação independente por ciclo e mantém o treinamento contínuo desativado. O feedback supervisionado usa 40 perguntas históricas em `feedback.txt`; a validação independente usa 20 perguntas novas em `validacao.txt`, e o teste reservado mantém 12 perguntas.

- Checkbox para habilitar o Tsukuyomi.
- **Épocas:** passagens completas pelo corpus em cada ciclo.
- **Tentativas:** quantidade de ciclos quando o treinamento contínuo estiver desligado. Em cada ciclo, o modelo treina pelas épocas configuradas usando somente os bytes das respostas IA:, recebe feedback das 40 perguntas dedicadas e seleciona checkpoints usando até 20 perguntas independentes.
- **Treinamento contínuo:** checkbox opcional. Quando ativada, encerra antecipadamente se todas as perguntas selecionadas forem aprovadas; o limite máximo de tentativas continua valendo. No modo de uma questão, encerra assim que essa questão for aprovada.

Na primeira execução, o modelo é inicializado. Nas execuções seguintes, o checkpoint persistido é avaliado antes de qualquer atualização e passa a ser o limite mínimo de qualidade da execução. Cada ciclo começa do melhor par checkpoint + momentos Adam da execução, nunca do último ciclo se ele tiver regredido. Um candidato também é rejeitado se fizer qualquer pergunta do holdout, anteriormente correta no melhor checkpoint, voltar a ficar incorreta; esse controle usa todas as 20 perguntas, mesmo quando o usuário seleciona menos perguntas para pontuar o ciclo. Pesos e momentos Adam são restaurados juntos. O melhor estado de uma execução não pode substituir um estado persistido superior. O workflow persiste na branch técnica `tsukuyomi-state` os pesos, os momentos Adam, o learning rate do próximo ciclo, o histórico por pergunta e a memória corretiva. O learning rate é retomado entre execuções e reduzido em 10% após cada ciclo, respeitando o piso de 1e-7. O histórico registra respostas geradas, acertos, regressões e recuperações por pergunta nos conjuntos de feedback e validação, incluindo observações entre execuções. As 40 perguntas de feedback são as únicas que geram correções supervisionadas; elas nunca participam da seleção de checkpoints nem da validação independente. As 20 perguntas novas de validação ficam fora do treinamento. O teste reservado permanece totalmente separado. O corpus original é imutável e as correções são deduplicadas. Isso é replay supervisionado, não RL. O gerador é iniciado uma vez por ciclo e carrega o checkpoint uma única vez. O conjunto de teste reservado não é consultado no modo de uma questão. Cada resposta termina no primeiro fim de linha gerado ou ao atingir 160 bytes. O relatório mostra respostas, gabaritos, métricas por pergunta, regressões e recuperações; os arquivos do modelo, do Adam, do scheduler, do histórico e do relatório ficam disponíveis como artefatos.

O avaliador é determinístico: remove diferenças de caixa, acentuação e pontuação e aprova somente igualdade lexical normalizada. O F1 lexical é exibido apenas como diagnóstico e nunca aprova sozinho uma resposta, reduzindo aprovações falsas por coincidência de palavras. Ainda não há avaliação semântica real: paráfrases corretas podem ser rejeitadas. A aprovação indica correspondência normalizada ao gabarito, não prova de compreensão.

O treinamento acumula gradientes em mini-lotes de 16 bytes-alvo por padrão, reduzindo as atualizações do Adam. O LR efetivo é adaptativo por pergunta: antes de cada ciclo, o gerador mede o F1 lexical atual de cada diálogo presente no corpus de treino e memória corretiva; a curva exponencial LR_individual = max(1e-7, LR_ciclo × 0,1^nível_lexical) aplica o LR-base em 0%, cerca de 12,6% do LR-base em 90% e 10% em 100%. Os mini-lotes são separados quando a taxa muda, para que uma pergunta nunca compartilhe uma atualização Adam com outra taxa. O mapa de intervalos é validado no executor C++ e só afeta bytes-alvo das respostas IA:. A retropropagação temporal é truncada em 64 etapas por padrão (configurável entre 1 e 64), enquanto o forward percorre sequencialmente todo o histórico disponível. O learning rate inicial padrão é 0,001 (0,1%), aceitando valores entre 1e-7 e 0,1; após cada ciclo, o executor reduz a taxa em 10% (multiplica por 0,9), independentemente da validação, respeitando o piso de 1e-7; a taxa do próximo ciclo é persistida e retomada entre execuções; a seleção prioriza o número de respostas exatas e usa F1/similaridade apenas para desempatar; Adam usa β1=0,9, β2=0,999 e epsilon=1e-8, com clipping global 1,0 e weight decay seletivo 0,01. Não há dropout na versão 0.1; o scheduler reduz o learning rate 10% a cada ciclo, sem depender da melhora da validação. O estado do Adam é salvo em arquivo separado com versão e checksum, junto do checkpoint. O formato Adam 8 invalida momentos produzidos com a representação contextual posicional anterior. O formato binário do checkpoint é a versão interna 8 para rejeitar pesos antigos incompatíveis; o Tsukuyomi continua na versão pública 0.1. A retomada exige os dois arquivos válidos. O workflow tem limite operacional de seis horas; por isso, os máximos de 1.000 épocas por ciclo e 100 ciclos são tetos configuráveis, e uma execução pode ser interrompida pelo limite de tempo antes de atingir o produto máximo de 100.000 épocas. A execução padrão é finita e não ativa treinamento contínuo.

## Escopo e limitações

A versão 0.1 continua sendo um modelo byte-level com orçamento de 10.000 parâmetros. Dois canais de características linguísticas aprendidas distinguem classes de bytes (letras, dígitos, espaços, pontuação e classes UTF-8) e fronteiras (início/fim/continuação de palavra, espaço, quebra de linha e fim de sentença). Esses canais reutilizam 13 linhas reservadas da tabela de embeddings (bytes inválidos UTF-8 `0xC0`, `0xC1` e `0xF5`–`0xFF`) e também enriquecem o contexto da pergunta; não introduzem tokenizer nem parâmetros adicionais nem colidem com texto UTF-8 válido. Para prompts U:/IA:, a pergunta inteira é representada por embeddings compartilhados agregados em 13 regiões posicionais contíguas, calculadas proporcionalmente ao comprimento; a média ainda comprime a ordem interna de cada região. O treinador agora aplica uma máscara de alvos em corpora U:/IA: e calcula a perda somente sobre os bytes das respostas IA:, incluindo o terminador de linha para que a geração aprenda quando encerrar a resposta. Os marcadores toleram BOM UTF-8 no início do arquivo e espaços/tabs antes de U: e IA:, mantendo o contexto anterior disponível. Isso torna o objetivo mais direcionado à resposta, mas não resolve a limitação de contexto: o modelo ainda não consegue reter a pergunta inteira quando ela excede a janela efetiva.

A recorrência atual usa retenção sigmoide aprendida com candidato tanh, sem implementar uma GRU/LSTM completa; há BPTT truncado de 64 etapas. Ainda não há atenção, memória externa ou Reinforcement Learning (RL). A avaliação lexical não fornece recompensa ao modelo nem atualiza os pesos. A geração preserva UTF-8 válido para prompts válidos, mas isso não comprova qualidade linguística ou compreensão. Consulte ARCHITECTURE.md.

O orçamento é conferido estaticamente e representa parâmetros implementados, não qualidade ou desempenho comprovados.

## Corpus conversacional inicial

O diretório data/ mantém treinamento-inicial.txt como corpus original de referência, com 40 pares de diálogo em português e 4.439 bytes em UTF-8. Para o workflow, o conjunto foi ampliado e separado em treinamento.txt (56 pares), feedback.txt (40 pares), validacao.txt (20 perguntas inéditas) e teste.txt (12 pares), sem perguntas repetidas entre os conjuntos. Os 32 diálogos originais escolhidos para treinamento continuam preservados; os outros oito diálogos originais permanecem fora do treino. A validação acompanha os ciclos; o teste reservado só é executado ao final.

Para treinar usando esse material, a partir de Morok/:

build/g2/morok-g2-tsukuyomi-train "Gerações/G2/Tsukuyomi/data/treinamento.txt" build/g2/tsukuyomi-inicial.ckpt 10 42

O arquivo é um corpus de modelagem de linguagem, não um conjunto de pares supervisionados com rótulos separados. O modelo aprende a prever a sequência inteira de bytes, incluindo os prefixos U: e IA:; isso não garante que ele aprenda a seguir o formato de diálogo ou responder corretamente a cada pergunta. A qualidade deve ser medida em exemplos que não estejam no corpus.

O executor avalia o checkpoint persistido antes do treinamento, seleciona o melhor estado pela validação independente e restaura o par pesos/Adam antes de cada ciclo seguinte e antes do teste reservado. Um checkpoint novo só substitui o anterior se superar sua classificação; assim, nenhuma execução pode degradar o melhor estado já persistido. O histórico JSON registra acertos, regressões e recuperações de cada pergunta ao longo dos ciclos e execuções. O learning rate e o histórico são persistidos na branch `tsukuyomi-state` junto dos pesos, Adam e feedback. O relatório registra train_bpb inicial/final, validação independente, resultados de feedback, regressões, recuperações, taxa de aprendizado e duração.


## Etapa 6 — generalização e benchmark

A Etapa 6 não aumenta a capacidade nominal nem altera os pesos do inventário. O pipeline passa a registrar, além da igualdade lexical normalizada, F1 lexical por frequência, similaridade de caracteres por sequência de edição, repetição consecutiva e diversidade de caracteres. O relatório apresenta médias globais e resultados separados por habilidade (fato, conceito, composição e conversa). Similaridade e F1 continuam sendo diagnósticos: nenhuma delas aprova uma resposta incorreta como correta.

O score composto 70/15/15 permanece no relatório como diagnóstico. Para selecionar checkpoints e controlar o scheduler, a ordem é lexicográfica: primeiro o número de respostas exatas; somente quando esse número empata, F1 e similaridade média podem desempatar, exigindo melhora mínima de 0,005. A cada ciclo concluído, o learning rate é reduzido em 10%, sem depender do resultado da validação, e nunca fica abaixo de 1e-7. Foram adicionados testes de ablação causal: zerar a projeção contextual deve remover a influência da pergunta; zerar a matriz recorrente deve remover a influência do histórico da resposta. Esses testes verificam que os dois caminhos arquiteturais realmente contribuem para o estado final.

A validação e o teste reservado continuam separados do treino, sem perguntas repetidas entre conjuntos. Os 32 diálogos originais permanecem preservados. A avaliação final continua usando o melhor checkpoint observado na validação e só consulta o teste reservado após o treinamento. A Etapa 6 melhora a capacidade de diagnosticar generalização; não implica que o modelo já tenha atingido qualidade conversacional satisfatória. Isso depende dos resultados medidos no benchmark. O workflow de treinamento registra essa avaliação com o novo score e mantém os limites independentes de até 1.000 épocas por ciclo e até 100 ciclos por execução, com treinamento contínuo desativado.


## Etapa 7 — scheduler orientado à generalização

O scheduler não usa a queda de train_bpb como evidência de melhora conversacional. O train_bpb continua sendo monitorado para diagnosticar ajuste ao corpus, mas a taxa de aprendizado e a seleção do melhor checkpoint são controladas pela validação. O ranking é lexicográfico: primeiro maximiza o número de respostas exatas; apenas quando esse número empata, considera a média de F1 lexical e similaridade de caracteres. O desempate exige ganho mínimo de 0,005 para evitar reagir a pequenas oscilações. Assim, respostas apenas parecidas nunca superam um checkpoint que responde corretamente a mais perguntas.

A cada ciclo concluído, o learning rate é reduzido em 10%, sem depender do resultado da validação, e nunca fica abaixo de 1e-7. O ranking lexicográfico continua selecionando o melhor checkpoint, mas não controla a taxa de aprendizado. A pontuação composta 70/15/15 permanece no relatório para comparação diagnóstica, mas não controla o ranking primário. Os pesos continuam limitados a 10.000 parâmetros; nenhum parâmetro, exemplo ou ciclo adicional foi introduzido.

## Etapa 8 — avaliação final e teste reservado

O workflow executa, depois de restaurar o melhor checkpoint selecionado exclusivamente pela validação, a avaliação final em 20 perguntas de validação independente (separadas do feedback histórico de 40 perguntas) e 12 perguntas reservadas de teste. O script `scripts/avaliacao_final.py` repete a geração do teste para verificar determinismo da decodificação gulosa, mede tempo total incluindo carga do checkpoint e vazão de perguntas por segundo, e registra exatidão lexical normalizada, F1, similaridade de caracteres, repetição e diversidade, com métricas agregadas por habilidade (fato, conceito, composição e conversa) no JSON. Os resultados são publicados em JSON e Markdown como artefatos do GitHub Actions. O JSON também compara o modelo com um baseline TF-IDF de recuperação sobre as perguntas de treinamento, nos mesmos conjuntos; a comparação histórica com a arquitetura anterior continua separada e só é marcada como disponível quando existe checkpoint compatível.

As métricas lexicais são diagnósticos e não comprovam compreensão semântica. A avaliação final não altera pesos, não treina sobre o teste e não altera a versão pública 0.1.0 nem o orçamento de 10.000 parâmetros. Os 32 exemplos originais permanecem preservados.

A comparação histórica entre a arquitetura anterior e a recorrente nova exige um checkpoint de baseline da arquitetura anterior e um executável capaz de carregá-lo. O script aceita esses dois caminhos como argumentos opcionais adicionais e executa ambos os modelos nos mesmos conjuntos de validação e teste, reportando as diferenças. Esse artefato não está presente no repositório atual; por isso, o workflow registra a comparação como indisponível em vez de inventar resultados. A avaliação final da arquitetura atual inclui a validação adaptativa e o teste reservado, que permanece independente do treinamento.
