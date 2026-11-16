# Relatório — G3 Etapa 5: validação, generalização e robustez

**Status da implementação:** concluída e integrada em `main`  
**Arquitetura avaliada:** G3 `0x0302`  
**Orçamento:** exatamente 100.000 parâmetros  
**Formato aceito pelo avaliador:** artefato `.morok` FP32 validado  
**Commit validado pela CI G3:** `fc5552097733ea9bba1ec51e1371f2b6cca1ec1a`

## 1. Objetivo

A Etapa 5 verifica se a G3 mantém desempenho em exemplos que não participaram do
treinamento e mede a sensibilidade do modelo a alterações superficiais da
entrada. O avaliador usa o backend neural G3 real e não modifica os pesos.

## 2. Separação de dados

A divisão é determinística e agrupada por `group_id`:

| Conjunto | Proporção | Uso |
|---|---:|---|
| Treino | 85% | Ajuste dos parâmetros |
| Validação | 10% | Acompanhamento durante o SFT |
| Teste | 5% | Avaliação final, sem atualização de pesos |

Todas as perguntas pertencentes ao mesmo `group_id` permanecem no mesmo
conjunto. O avaliador recria a divisão com a mesma seed usada no treinamento.
A seed deve ser passada ao comando de avaliação quando não for a padrão 42.

## 3. Métricas calculadas

O relatório JSON inclui:

- contagem real de parâmetros, que deve ser 100.000;
- fingerprint do dataset;
- quantidade de exemplos e grupos em cada split;
- loss causal média em treino, validação e teste limpo;
- diferença de loss validação-treino;
- diferença de loss teste-treino;
- razão entre loss de teste e loss de treino;
- alerta heurístico de overfitting;
- acurácia byte-level por condição;
- degradação de loss e acurácia em relação à pergunta limpa.

A loss e a acurácia são calculadas sob teacher forcing, sobre os bytes da
resposta e o token EOS. A acurácia é predição exata do próximo byte; não deve
ser interpretada como avaliação semântica humana da resposta.

## 4. Matriz de robustez

Cada pergunta do conjunto de teste é processada nas seguintes condições:

| Condição | Transformação |
|---|---|
| Limpa | Pergunta original |
| Erro tipográfico | Transposição determinística de letras adjacentes |
| Sem acentos | Decomposição Unicode NFD e remoção de marcas combinantes |
| Pontuação | Remoção de pontuação e inclusão de interrogação |
| Espaçamento | Espaços repetidos e margens irregulares |
| Unicode e emojis | Inclusão de caracteres não latinos, acentos e emojis |

As transformações são criadas em memória durante a avaliação. Elas não alteram
o dataset original e não entram no treinamento.

## 5. Critério de overfitting e regressões

O alerta de overfitting é ativado quando a loss de validação ou teste excede a
loss de treino em mais de 25% do valor de referência de treino. Esse limite é
configurável e é um sinal de investigação, não uma prova matemática de
overfitting.

A regressão de robustez é medida pela diferença de loss e acurácia de cada
condição em relação à pergunta limpa no mesmo artefato. Para medir regressão
entre versões do modelo, é necessário executar o mesmo dataset, seed e
protocolo contra os dois artefatos e comparar os relatórios.

## 6. Execução

Depois de treinar e exportar o modelo FP32:

```sh
morok-g3-evaluate dataset.jsonl modelo.morok relatorio-generalizacao.json 42
```

O quarto argumento é a seed do split. O avaliador valida o cabeçalho, checksum,
dimensões, arquitetura e payload do artefato antes de carregar os pesos. Artefatos
INT8 não são aceitos nesta avaliação FP32.

## 7. Verificação automatizada

A workflow G3 executou no commit indicado acima. Resultado observado:

| Teste | Resultado |
|---|---|
| `g3.neural` | Aprovado |
| `g3.inference` | Aprovado |
| `g3.training` | Aprovado |
| `g3.evaluation` | Aprovado |
| `g3.training_cli` | Aprovado |

**Resultado:** 5 de 5 testes aprovados; 0 falhas. A compilação dos alvos
`morok_g3_evaluate` e `morok_test_g3_evaluation` também foi concluída.

Workflow: https://github.com/korczaktech/Morok-AI/actions/runs/43843799083

## 8. Resultado empírico de produção — pendente de corpus

Não existe um corpus de produção revisado nem um artefato G3 treinado com esse
corpus disponível no repositório nesta etapa. Por isso, não há números
representativos de generalização semântica, robustez real ou taxa de
overfitting para publicar agora.

Os testes automatizados usam dados sintéticos e verificam a integridade do
protocolo, a leitura do artefato, as transformações, o cálculo das métricas e a
geração do relatório. Eles não demonstram que o modelo já responde bem a
perguntas inéditas.

O relatório quantitativo de produção será produzido pelo comando acima assim
que houver um dataset real com proveniência, grupos de intenção, exemplos
suficientes e respostas de referência revisadas. O conjunto de teste deverá
permanecer congelado para comparações entre versões.

## 9. Conclusão

A infraestrutura de validação, separação por grupos, avaliação byte-level,
testes de robustez, cálculo de gaps e emissão do relatório foi implementada e
passou pela CI específica da G3. A arquitetura permanece em 100.000 parâmetros.

A aprovação da implementação não deve ser confundida com aprovação da
qualidade empírica do modelo. Essa última depende da execução sobre um corpus
real e da análise dos valores produzidos.
