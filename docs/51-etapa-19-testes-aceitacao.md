# Etapa 19 — Testes de aceitação

## Objetivo

Verificar que a G1 e os sete subsistemas funcionam em fluxos completos e integrados, acima da cobertura unitária e de integração introduzida nas etapas anteriores. A etapa valida contratos observáveis, artefatos produzidos, isolamento e encerramento; não é uma campanha de qualidade linguística nem o primeiro treinamento completo de referência.

## Suíte adicionada

### 1. `acceptance.g1_training_checkpoint_generation`

Arquivo: `tests/acceptance/g1/test_g1_acceptance.cpp`.

Executa um fluxo G1 determinístico em corpus de fixture versionado:

1. Confirma em compilação e execução o inventário de exatamente 1.000 parâmetros treináveis.
2. Inicializa os parâmetros com semente fixa e verifica finitude.
3. Treina por uma época sobre documentos UTF-8 de fixture, com partições treino/validação/teste não vazias.
4. Exige atualizações reais do otimizador e alteração do vetor de parâmetros.
5. Exige checkpoint de retomada, melhor modelo e log JSONL.
6. Carrega o checkpoint e compara os 1.000 parâmetros com o estado treinado.
7. Executa inferência autoregressiva limitada e valida a saída UTF-8.
8. Codifica e decodifica um corpus repetitivo pelo RLAH comprimido, exigindo round-trip byte a byte.
9. Corrompe o artefato RLAH e exige rejeição por integridade.

O fixture é exclusivamente um corpus técnico sintético de teste; seu resultado não pode ser usado como evidência de qualidade linguística real.

### 2. `acceptance.seven_subsystems`

Arquivo: `tests/acceptance/end_to_end/test_seven_subsystems.cpp`.

Inicializa IA-CORE Runtime com os seis subsistemas cognitivos IA-MEM, IA-LANG, IA-REAS, IA-TOOLS, IA-COMM e IA-LEARN, além da camada complementar IA-SECURITY. Depois:

- confirma que os sete subsistemas estão ativos;
- cria conversa, sessão e RequestContext correlacionado;
- grava, recupera e pesquisa uma memória associada à sessão;
- cria e executa um plano IA-REAS determinístico;
- registra e invoca uma ferramenta permitida através do wrapper de autorização IA-SECURITY;
- executa um experimento real de uma época via IA-LEARN e verifica RequestId, relatório, finitude do candidato e isolamento dos parâmetros originais;
- envia mensagem por IA-COMM e IA-LANG e verifica resposta/histórico;
- limpa memória de trabalho e estado de segurança da sessão;
- encerra conversa e runtime e verifica estado final de todos os subsistemas.

## Integração com CTest

Os dois executáveis são registrados como testes com rótulo `acceptance`, timeout explícito e suporte aos sanitizers já configurados pelo projeto. Continuam ativos em builds Release, assim como os demais testes assert-based.

Comandos locais:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure

cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
```

Para executar apenas a camada de aceitação:

```sh
ctest --test-dir build/debug -L acceptance --output-on-failure
```

## Critérios de aprovação

A etapa 19 exige, no mesmo commit:

- GCC Debug e Release aprovados;
- Clang Debug e Release aprovados;
- ASan/UBSan aprovados;
- fuzzers existentes aprovados;
- ambos os testes de aceitação aprovados;
- nenhum teste unitário, de integração, propriedade ou regressão preexistente quebrado.

A conclusão da implementação dos testes não é confundida com aprovação de CI: o status só é declarado depois que as execuções do GitHub Actions do HEAD final terminarem com sucesso.

## O que esta etapa não afirma

- Não afirma que a G1 já possui qualidade conversacional, compreensão semântica ou desempenho competitivo.
- Não afirma que o corpus sintético substitui corpus real/licenciado.
- Não realiza o primeiro treinamento completo da G1; isso pertence à etapa 20.
- Não mede o comportamento em hardware físico de 4 GB; a medição real continua sendo um experimento separado.
- Não implementa treinamento online, promoção automática de pesos ou novas capacidades de subsistemas.

## Resultado esperado

A etapa 19 fica concluída quando os dois fluxos de aceitação estiverem integrados ao CTest, executarem com sucesso nos perfis obrigatórios e o CI do commit final estiver verde. Os relatórios devem preservar a distinção entre teste sintético de funcionamento e evidência experimental de qualidade.
