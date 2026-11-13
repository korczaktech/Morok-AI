# 2. Arquitetura geral

**DEC-ARCH-001 — Arquitetura experimental própria**  
**DEC-ARCH-002 — C++ como linguagem principal**  
**DEC-ARCH-003 — Separação de responsabilidades**

## 2.1 Visão em camadas

O projeto será organizado em cinco áreas técnicas independentes:

1. **Modelo:** estrutura matemática e parâmetros treináveis.
2. **Motor de inferência:** executa operações do modelo e produz previsões.
3. **Treinador:** calcula objetivos, gradientes e atualizações dos parâmetros.
4. **Representação linguística:** converte entrada em unidades internas e reconstrói saídas.
5. **Sistema cognitivo:** coordena os sete subsistemas e os serviços de nível superior.

A arquitetura neural poderá mudar sem obrigar a reconstrução do sistema cognitivo. As interfaces entre as áreas deverão ser explícitas e versionadas.

## 2.2 Separação entre modelo e sistema cognitivo

O modelo de linguagem não será confundido com toda a IA. O modelo é um componente treinável que estima padrões linguísticos. Memória persistente, planejamento, ferramentas, permissões e comunicação pertencem a componentes do sistema cognitivo.

O sistema cognitivo poderá utilizar o modelo, mas não deverá depender de detalhes internos específicos de uma geração neural.

## 2.3 Interfaces conceituais

As interfaces deverão definir:
- formato de entrada e saída;
- representação das unidades;
- estado de inferência;
- carregamento e salvamento de parâmetros;
- configuração de treinamento;
- métricas e diagnósticos;
- versão do formato de checkpoint.

Os contratos exatos ainda serão detalhados antes da implementação.

## 2.4 Requisitos de engenharia

- C++ como implementação principal.
- Evitar cópias desnecessárias de buffers.
- Preferir processamento por fluxo e lotes pequenos.
- Controlar alocações e vida útil dos estados.
- Separar código de produção, testes e ferramentas experimentais.
- Não acoplar a arquitetura a uma biblioteca de modelos externos.
- Bibliotecas numéricas auxiliares poderão ser avaliadas, mas a escolha deverá ser documentada e não poderá introduzir dependência de pesos pré-treinados.

## 2.5 Evolução por gerações

Cada geração terá:
- orçamento de parâmetros;
- configuração de arquitetura;
- formato de checkpoint;
- conjunto de treinamento e validação identificado;
- métricas de qualidade e desempenho;
- relatório comparativo com a geração anterior.

A ampliação de parâmetros não garante aumento de qualidade. Cada etapa deverá demonstrar ganhos mensuráveis.
