# Tsukuyomi — G2

**Geração:** Segunda Geração (G2)  
**Modelo:** Tsukuyomi  
**Versão:** 0.1.0  
**Perfil:** modelo mais avançado da linha G2  
**Orçamento:** exatamente 10.000 parâmetros treináveis

O Tsukuyomi prioriza capacidade e tarefas complexas. A versão 0.1 entrega um modelo neural autoregressivo byte-level, com embeddings compartilhados, contexto causal, treinamento Adam, geração greedy e checkpoints versionados. A implementação é privada da G2 e não importa código da G1.

## Estrutura

- include/morok/g2/tsukuyomi/model.hpp: contrato público do modelo.
- src/model.cpp: parâmetros, inicialização, forward pass, avaliação, gradientes, Adam, geração e checkpoints.
- apps/train.cpp: treinamento de corpus local.
- apps/generate.cpp: inferência a partir de checkpoint.
- tests/test_model.cpp: invariantes, aprendizado, geração e persistência.
- ARCHITECTURE.md: especificação matemática, inventário e limitações.

## Compilação

A partir de Morok/:

cmake -S . -B build/g2 -DCMAKE_BUILD_TYPE=Release -DMOROK_BUILD_TESTS=ON -DMOROK_BUILD_APPS=ON

cmake --build build/g2 --target morok_g2_tsukuyomi morok_g2_tsukuyomi_train morok_g2_tsukuyomi_generate morok_test_g2_tsukuyomi --parallel 2

ctest --test-dir build/g2 -R g2.tsukuyomi.model --output-on-failure

## Treinamento e inferência

build/g2/morok-g2-tsukuyomi-train corpus.bin tsukuyomi-g2-v0.1.ckpt 10 42

build/g2/morok-g2-tsukuyomi-generate tsukuyomi-g2-v0.1.ckpt "Olá" 120

O corpus é um arquivo binário de 2 bytes a 512 MiB. O modelo não inclui corpus de treinamento.

## Escopo e limitações

A versão 0.1 é um pipeline funcional de modelo de linguagem byte-level, não uma demonstração de raciocínio geral. O contexto é finito; não há BPTT, memória externa, tokenizer, subsistemas cognitivos G2, retomada do estado do otimizador ou garantia de UTF-8 na saída. Qualidade linguística depende de treinamento e avaliação com corpus apropriado. Consulte ARCHITECTURE.md.

O orçamento é conferido estaticamente e representa parâmetros implementados, não qualidade ou desempenho comprovados.
