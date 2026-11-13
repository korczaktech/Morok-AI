# 2. Arquitetura geral

**DEC-ARCH-001 — Arquitetura experimental própria**  
**DEC-ARCH-002 — C++20 como linguagem principal**  
**DEC-ARCH-003 — Separação de responsabilidades**

## 2.1 Visão em camadas

O projeto será organizado em cinco áreas técnicas independentes:

1. **Modelo:** estrutura matemática e parâmetros treináveis.
2. **Motor de inferência:** executa operações do modelo e produz previsões.
3. **Treinador:** calcula objetivos, gradientes e atualizações.
4. **Representação linguística:** converte entrada em unidades internas e reconstrói saídas.
5. **Sistema cognitivo:** coordena os sete subsistemas e serviços de nível superior.

A arquitetura neural poderá mudar sem obrigar a reconstrução do sistema cognitivo. Interfaces públicas são especificadas no documento 19.

## 2.2 Separação entre modelo e sistema cognitivo

O modelo de linguagem é um componente treinável que estima padrões linguísticos. Memória persistente, planejamento, ferramentas, permissões e comunicação pertencem a componentes separados. O sistema cognitivo acessa o modelo somente por interfaces versionadas.

## 2.3 Interfaces

Contratos definem:
- formatos de entrada e saída;
- bytes e texto UTF-8 validado;
- estado de inferência e isolamento de sessão;
- carregamento e salvamento de parâmetros;
- configuração de treinamento;
- métricas e diagnósticos;
- erros tipados e cancelamento;
- versões de API e checkpoint.

Detalhes normativos: documento 19.

## 2.4 Requisitos de engenharia

- C++20 e build CMake.
- Evitar cópias desnecessárias de buffers.
- Preferir processamento por fluxo e lotes pequenos.
- Controlar alocações e vida útil dos estados.
- Separar código, testes e ferramentas experimentais.
- Não acoplar a arquitetura a modelos externos.
- Dependências auxiliares devem ser documentadas e justificadas.

## 2.5 Evolução por gerações

Cada geração terá orçamento de parâmetros, arquitetura, formato de checkpoint, corpus identificado, métricas, relatório comparativo e política de migração. A ampliação de parâmetros não garante aumento de qualidade.
