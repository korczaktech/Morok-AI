# 1. Visão geral do projeto

**Versão:** 0.2  
**Estado:** conceitual, decisões iniciais consolidadas  
**Linguagem principal:** C++  
**Memória física inicial disponível:** 4 GB

## 1.1 Objetivo

Desenvolver uma inteligência artificial própria, incluindo um modelo de linguagem treinado do zero, sem utilizar pesos pré-treinados e sem depender de outro modelo para realizar inferência.

O projeto é de longo prazo. O desenvolvimento começa com uma arquitetura pequena, experimental e mensurável, que poderá crescer por gerações.

A primeira meta não é competir com modelos comerciais. É construir uma base tecnicamente correta, reproduzível e capaz de demonstrar aprendizado real.

## 1.2 Princípios

- **Originalidade investigada:** propor e testar mecanismos próprios, sem afirmar novidade científica antes de revisar a literatura.
- **Modularidade:** separar componentes com responsabilidades distintas.
- **Escalabilidade:** permitir evoluir o número de parâmetros e a capacidade sem reconstruir todo o sistema.
- **Eficiência:** medir operações, memória, latência e custo de treinamento.
- **Reprodutibilidade:** registrar sementes, dados, configurações, versões e checkpoints.
- **Independência:** não utilizar pesos externos para inferência.
- **Mensurabilidade:** validar cada hipótese com testes e baselines.
- **Simplicidade inicial:** evitar mecanismos adaptativos ou controladores mais complexos que o benefício que entregam.

## 1.3 Restrições iniciais

A máquina de referência dispõe de 4 GB de RAM. A arquitetura deverá permitir experimentos pequenos, lotes reduzidos e processamento incremental.

A meta inicial é manter o processo do projeto próximo ou abaixo de 512 MB quando viável, com teto operacional inicial de 1 GB. Esses números são metas provisórias e deverão ser medidos no ambiente real.

## 1.4 Treinamento do zero

O estado inicial dos parâmetros será inicializado aleatoriamente segundo regras definidas na especificação matemática. O modelo não importará pesos de outros modelos.

Dados de treinamento poderão ser textos e corpora devidamente licenciados ou de uso permitido. Dados não são pesos pré-treinados; ainda assim, sua origem, licença, limpeza e divisão entre treinamento e validação deverão ser documentadas.

## 1.5 Terminologia

- **Parâmetro treinável:** valor numérico atualizado pelo algoritmo de treinamento.
- **Estado:** informação temporária mantida durante o processamento.
- **Memória externa:** dados persistidos fora dos parâmetros do modelo.
- **Unidade:** elemento aprendido ou representado pela RLAH.
- **Agrupamento:** associação de unidades menores em uma unidade de escala maior.
- **Inferência:** execução do modelo para produzir previsões.
- **Treinamento:** processo de ajustar parâmetros com dados e uma função objetivo.
- **RLAH:** nome provisório da proposta de representação latente adaptativa hierárquica.

## 1.6 Critério de progresso

Uma etapa só será marcada como concluída quando houver:
1. especificação escrita;
2. implementação correspondente;
3. testes automatizados;
4. medição de recursos;
5. comparação com uma referência adequada;
6. registro de limitações conhecidas.
