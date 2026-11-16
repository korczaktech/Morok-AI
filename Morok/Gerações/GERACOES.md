# Definições oficiais das gerações do Morok AI

Este documento registra a organização das gerações, os modelos previstos em cada uma, suas quantidades de parâmetros e as regras de nomenclatura e versionamento. Ele funciona como referência independente; os arquivos `README.md` não foram alterados.

## 1. Como as gerações são organizadas

Cada geração representa uma etapa de evolução do projeto. A progressão considera principalmente:

- aumento da capacidade parametrizada dos modelos;
- amadurecimento das capacidades de geração;
- ampliação das funções que os modelos podem executar;
- introdução e expansão de arquiteturas compostas por agentes especializados;
- evolução gradual do estágio experimental para modelos de uso real.

A quantidade de parâmetros indicada para cada modelo é a dimensão definida para aquela versão da arquitetura. Ela não deve ser interpretada, isoladamente, como prova de qualidade, inteligência, compreensão ou desempenho. Esses aspectos precisam ser avaliados por testes e métricas próprios.

## 2. Convenção de versões

O padrão oficial é:

`GX.Y.Z`

Cada parte possui um significado específico:

- **X — geração:** identifica a geração principal do modelo. Por exemplo, `G3` identifica a terceira geração.
- **Y — versão geracional:** identifica a evolução principal do modelo dentro daquela geração. Uma mudança nesse número representa uma nova versão geracional.
- **Z — ajuste fino:** identifica revisões menores e ajustes do modelo dentro da mesma versão geracional.

Exemplos:

- `G2.2.0`: geração 2, versão geracional 2, ajuste fino 0.
- `G3.1.0`: geração 3, versão geracional 1, ajuste fino 0.
- `G4.1.2`: geração 4, versão geracional 1, ajuste fino 2.

O número da geração não deve ser confundido com o número da versão geracional. Em `G2.2.0`, por exemplo, o primeiro 2 indica a geração e o segundo 2 indica a versão dentro dela.

## 3. Unidades e siglas

As abreviações usadas nas especificações significam:

| Sigla | Significado | Equivalência |
|---|---|---:|
| Mi | Milhão | 1.000.000 |
| Bi | Bilhão | 1.000.000.000 |
| T | Trilhão | 1.000.000.000.000 |
| AC | Agente Construtor | Unidade de construção especializada da arquitetura Artemis |
| AO | Agente Orquestrador | Unidade responsável pela orquestração dos agentes construtores na arquitetura Artemis |

Quando uma arquitetura é descrita como `5 AC × 1.000.000 + 1 AO × 10.000.000`, isso significa cinco Agentes Construtores com um milhão de parâmetros cada e um Agente Orquestrador com dez milhões de parâmetros. A expressão descreve a distribuição de parâmetros entre os componentes, não uma multiplicação entre agentes.

## 4. Geração 1 — G1

**Objetivo:** experimentação e familiarização com Inteligência Artificial e Aprendizado de Máquina.

A G1 é a geração inicial de testes. Seu propósito é permitir o estudo prático de redes neurais, treinamento, inferência, parâmetros e avaliação, antes da evolução para modelos destinados à geração real.

- **Modelo:** Tsukuyomi Max.
- **Quantidade:** 1.000 parâmetros.
- **Finalidade:** experimentação, testes e aprendizado sobre IA e ML.
- **Estágio:** geração experimental.

A G1 não deve ser confundida com as gerações posteriores de geração real. Seu papel é servir como base de aprendizado e validação das ideias iniciais do projeto.

## 5. Geração 2 — G2

**Objetivo:** introduzir geração real e capacidades práticas de execução.

A G2 marca a transição da fase de familiarização para uma geração capaz de produzir respostas em uso real. Também contempla a capacidade de executar comandos e disponibilizar o modelo publicamente.

- **Modelo:** Tsukuyomi.
- **Versão definida:** Tsukuyomi G2.2.0.
- **Quantidade:** 10.000 parâmetros.
- **Capacidades previstas:** geração real, execução de comandos e disponibilização pública.
- **Estágio:** geração real.

A versão `G2.2.0` segue a convenção de versionamento descrita neste documento: geração 2, versão geracional 2 e ajuste fino 0.

## 6. Geração 3 — G3

**Objetivo:** desenvolver uma família de modelos com capacidades e dimensões diferentes.

A G3 está em desenvolvimento e estabelece três modelos:

| Modelo | Versão | Parâmetros |
|---|---|---:|
| Tsukuyomi | G3.1.0 | 100.000 |
| Apollo | G3.1.0 | 50.000 |
| Mokosh | G3.1.0 | 20.000 |

Cada modelo possui uma dimensão própria dentro da geração. As quantidades são especificações individuais e não devem ser somadas como se os três modelos formassem uma única rede.

- **Tsukuyomi:** modelo definido com a maior quantidade de parâmetros da família G3.
- **Apollo:** modelo definido com dimensão intermediária.
- **Mokosh:** modelo definido com a menor dimensão da família G3.
- **Estágio da geração:** em desenvolvimento.

## 7. Geração 4 — G4

**Objetivo:** ampliar a capacidade dos três modelos estabelecidos na G3.

| Modelo | Versão | Parâmetros |
|---|---|---:|
| Tsukuyomi | G4.1.0 | 1.000.000 |
| Apollo | G4.1.0 | 500.000 |
| Mokosh | G4.1.0 | 200.000 |

A G4 mantém a família de três modelos da G3 e aumenta suas dimensões. Nesta geração, a especificação apresentada não inclui Artemis.

- **Estágio:** planejada.

## 8. Geração 5 — G5

**Objetivo:** ampliar a família de modelos e introduzir a arquitetura Artemis.

| Modelo | Versão | Parâmetros |
|---|---|---:|
| Tsukuyomi | G5.1.0 | 10.000.000 |
| Apollo | G5.1.0 | 5.000.000 |
| Mokosh | G5.1.0 | 2.000.000 |

A G5 introduz também o **Artemis**, uma arquitetura composta por agentes construtores e um agente orquestrador:

- **5 AC:** cada Agente Construtor possui 1.000.000 de parâmetros.
- **1 AO:** o Agente Orquestrador possui 10.000.000 de parâmetros.

Distribuição do Artemis G5.1.0:

`5 AC × 1.000.000 + 1 AO × 10.000.000`

O total aritmético dessa configuração é de 15.000.000 de parâmetros distribuídos entre os seis componentes.

- **Estágio:** planejada.

## 9. Geração 6 — G6

**Objetivo:** escalar a família de modelos e a arquitetura Artemis.

| Modelo | Versão | Parâmetros |
|---|---|---:|
| Tsukuyomi | G6.1.0 | 100.000.000 |
| Apollo | G6.1.0 | 50.000.000 |
| Mokosh | G6.1.0 | 20.000.000 |

Configuração do Artemis G6.1.0:

- **5 AC:** 10.000.000 de parâmetros por agente.
- **1 AO:** 100.000.000 de parâmetros.

`5 AC × 10.000.000 + 1 AO × 100.000.000`

Total aritmético distribuído: 150.000.000 de parâmetros.

- **Estágio:** planejada.

## 10. Geração 7 — G7

**Objetivo:** continuar a expansão de escala dos modelos e do Artemis.

| Modelo | Versão | Parâmetros |
|---|---|---:|
| Tsukuyomi | G7.1.0 | 1.000.000.000 |
| Apollo | G7.1.0 | 500.000.000 |
| Mokosh | G7.1.0 | 200.000.000 |

Configuração do Artemis G7.1.0:

- **5 AC:** 100.000.000 de parâmetros por agente.
- **1 AO:** 1.000.000.000 de parâmetros.

`5 AC × 100.000.000 + 1 AO × 1.000.000.000`

Total aritmético distribuído: 1.500.000.000 de parâmetros.

- **Estágio:** planejada.

## 11. Geração 8 — G8

**Objetivo:** ampliar a arquitetura Artemis para dez Agentes Construtores e elevar a escala geral dos modelos.

| Modelo | Versão | Parâmetros |
|---|---|---:|
| Tsukuyomi | G8.1.0 | 10.000.000.000 |
| Apollo | G8.1.0 | 5.000.000.000 |
| Mokosh | G8.1.0 | 2.000.000.000 |

Configuração do Artemis G8.1.0:

- **10 AC:** 750.000.000 de parâmetros por agente.
- **1 AO:** 15.000.000.000 de parâmetros.

`10 AC × 750.000.000 + 1 AO × 15.000.000.000`

Total aritmético distribuído: 22.500.000.000 de parâmetros.

- **Estágio:** planejada.

## 12. Geração 9 — G9

**Objetivo:** escalar a família de modelos para a ordem das centenas de bilhões de parâmetros e manter a arquitetura Artemis com dez construtores.

| Modelo | Versão | Parâmetros |
|---|---|---:|
| Tsukuyomi | G9.1.0 | 100.000.000.000 |
| Apollo | G9.1.0 | 50.000.000.000 |
| Mokosh | G9.1.0 | 20.000.000.000 |

Configuração do Artemis G9.1.0:

- **10 AC:** 7.500.000.000 de parâmetros por agente.
- **1 AO:** 150.000.000.000 de parâmetros.

`10 AC × 7.500.000.000 + 1 AO × 150.000.000.000`

Total aritmético distribuído: 225.000.000.000 de parâmetros.

- **Estágio:** planejada.

## 13. Geração 10 — G10

**Objetivo:** estabelecer a escala de um trilhão de parâmetros para Tsukuyomi e ampliar proporcionalmente os demais modelos.

| Modelo | Versão | Parâmetros |
|---|---|---:|
| Tsukuyomi | G10.1.0 | 1.000.000.000.000 (1 T) |
| Apollo | G10.1.0 | 500.000.000.000 (500 Bi) |
| Mokosh | G10.1.0 | 200.000.000.000 (200 Bi) |

Configuração do Artemis G10.1.0:

- **10 AC:** 75.000.000.000 de parâmetros por agente.
- **1 AO:** 1.500.000.000.000 de parâmetros (1,5 T).

`10 AC × 75.000.000.000 + 1 AO × 1.500.000.000.000`

Total aritmético distribuído: 2.250.000.000.000 de parâmetros (2,25 T).

- **Estágio:** planejada.

## 14. Visão consolidada

A tabela abaixo resume as quantidades definidas para Tsukuyomi, Apollo e Mokosh. Os valores de Artemis são detalhados nas seções de cada geração, pois sua configuração é distribuída entre ACs e AO.

| Geração | Tsukuyomi | Apollo | Mokosh | Situação |
|---|---:|---:|---:|---|
| G1 | 1.000 | — | — | Experimental |
| G2 | 10.000 | — | — | Geração real |
| G3 | 100.000 | 50.000 | 20.000 | Em desenvolvimento |
| G4 | 1 Mi | 500 mil | 200 mil | Planejada |
| G5 | 10 Mi | 5 Mi | 2 Mi | Planejada |
| G6 | 100 Mi | 50 Mi | 20 Mi | Planejada |
| G7 | 1 Bi | 500 Mi | 200 Mi | Planejada |
| G8 | 10 Bi | 5 Bi | 2 Bi | Planejada |
| G9 | 100 Bi | 50 Bi | 20 Bi | Planejada |
| G10 | 1 T | 500 Bi | 200 Bi | Planejada |

O traço (`—`) indica que este documento não define um modelo correspondente para aquela geração; não significa que exista um modelo com zero parâmetros.

## 15. Regras de interpretação

1. Cada quantidade de parâmetros pertence ao modelo ou componente indicado. Não se deve atribuir a quantidade de um modelo a outro.
2. Na arquitetura Artemis, os parâmetros dos ACs e do AO são apresentados separadamente. O total aritmético é a soma das quantidades de todos os componentes.
3. A presença de um modelo nesta especificação não significa que sua implementação, treinamento ou disponibilização já tenham sido concluídos.
4. Os estágios descritos distinguem o que está experimental, em desenvolvimento ou planejado. Uma especificação futura não deve ser apresentada como capacidade já disponível.
5. Alterações futuras nas quantidades, na composição dos agentes ou no versionamento devem ser registradas nesta referência, preservando o histórico das decisões.
