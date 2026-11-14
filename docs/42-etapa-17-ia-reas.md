# Etapa 17 — IA-REAS: raciocínio simbólico e planejamento

## Objetivo

Implementar a primeira versão funcional de IA-REAS conforme os contratos dos documentos 19 e 20. Esta versão é um planejador simbólico determinístico para tarefas estruturadas. Não interpreta linguagem natural e não é apresentada como raciocínio geral.

## Entrada estruturada

Uma tarefa contém:
- objetivo textual UTF-8 limitado;
- valores iniciais explicitamente fornecidos pelo chamador;
- contexto autorizado, também fornecido explicitamente;
- pré-condições globais;
- etapas tipadas;
- dependências declaradas e inferidas a partir das variáveis produzidas/consumidas;
- orçamento máximo de etapas;
- política de falha.

Valores suportados: número finito double, booleano e string UTF-8. Nomes de variáveis e etapas são identificadores ASCII limitados. O módulo não consulta IA-MEM por conta própria: o chamador deve recuperar e autorizar o contexto antes de construir a tarefa.

## Operações determinísticas

- atribuição de literal;
- soma, subtração, multiplicação e divisão;
- igualdade e comparação menor que;
- AND, OR e NOT booleanos;
- concatenação de strings UTF-8.

Operações numéricas rejeitam operandos incompatíveis, divisão por zero e resultados não finitos. Operações lógicas exigem booleanos. Comparações ordenadas aceitam números finitos ou strings do mesmo tipo. Nenhuma operação executa código arbitrário, shell, rede ou ferramenta externa.

## Planejamento

1. Validar objetivo, valores, identificadores, assinaturas e orçamento.
2. Verificar que toda entrada tem valor inicial autorizado ou produtor.
3. Resolver dependências explícitas e inferidas.
4. Ordenar deterministicamente as etapas com desempate pela ordem original.
5. Detectar ciclos, dependências ausentes e auto-dependências.
6. Calcular profundidade do grafo e rejeitar planos fora dos limites.
7. Vincular o plano ao SessionId e RequestId que o originaram.

Limites máximos desta versão: 32 etapas, profundidade de dependência 8, objetivo 512 bytes, valor individual 4 KiB e uma tentativa de recuperação por etapa. Os limites são configuráveis para valores menores; os máximos de segurança não podem ser ampliados nesta versão.

## Execução

O executor revalida a identidade do plano, verifica pré-condições, dependências e tipos, executa cada operação determinística e valida pós-condições antes de publicar o valor produzido. Uma etapa não pode sobrescrever valores iniciais nem a saída de outra etapa.

Uma etapa pode declarar um valor alternativo (fallback). Se a operação falhar ou a pós-condição não for satisfeita, o executor pode tentar esse valor uma única vez; ele só é aceito se passar pela pós-condição. O resultado informa etapas concluídas, recuperadas, falhas e ignoradas.

Políticas:
- abort_plan: interrompe no primeiro erro.
- continue_independent: registra a falha, ignora etapas dependentes dela e continua etapas independentes.

Cancelamento é verificado antes do planejamento, durante a ordenação, antes de cada etapa e após a última etapa. Operações são curtas e limitadas; não há execução assíncrona.

## Isolamento, segurança e ciclo de vida

- O plano é vinculado à sessão e à requisição de origem; outra requisição não pode executá-lo.
- Contexto é passado explicitamente pelo chamador; não existe leitura implícita de memória.
- A execução não possui interface de ferramentas nem permissões de sistema operacional.
- O subsistema participa do ciclo de vida IA-CORE por callbacks de inicialização e encerramento.
- Planejamento e execução são serializados com o encerramento para evitar shutdown durante uma operação ativa.
- Erros retornam Result<T>/Status com código, subsistema, estágio e RequestId.
- Alocações críticas são convertidas em OUT_OF_MEMORY.

## Arquivos

- include/morok/reason/reasoning_subsystem.hpp
- src/reason/reasoning_subsystem.cpp
- tests/unit/reason/test_reasoning_subsystem.cpp
- CMakeLists.txt
- tests/CMakeLists.txt

## Testes de aceitação

O teste reason.reasoning_subsystem cobre:
1. inicialização e encerramento via IA-CORE Runtime;
2. plano com dependências inferidas e ordem determinística;
3. soma, subtração, multiplicação, divisão, igualdade e comparação;
4. operações booleanas e concatenação;
5. pré-condições e pós-condições;
6. recuperação por fallback;
7. política abortar e continuar etapas independentes;
8. detecção de ciclo, entrada ausente e profundidade excessiva;
9. orçamento de etapas;
10. isolamento por RequestId;
11. cancelamento;
12. rejeição de chamadas após shutdown.

## Limitações explícitas

- Não converte prompts livres em tarefas estruturadas.
- Não afirma raciocínio geral, compreensão semântica ou capacidade de prova.
- Não chama IA-LANG, IA-MEM, IA-TOOLS nem serviços externos automaticamente.
- Não executa planos em paralelo.
- Não oferece persistência, histórico de planos ou aprendizagem de estratégias.
- A política continue_independent retorna resultados parciais com listas de falhas e etapas ignoradas; não transforma falhas em sucesso.
- Os valores numéricos usam double; não há aritmética inteira exata ou simbólica algébrica.

## Critério de conclusão

IA-REAS está implementada quando aceita tarefas tipadas, valida valores e restrições, constrói ordem topológica determinística, detecta ciclos e limites, executa operações matemáticas/lógicas seguras, aplica pré/pós-condições, respeita política de falha e cancelamento, vincula planos a requisições e possui teste automatizado registrado no CTest. A validação final exige que GCC, Clang e sanitizers aprovem os workflows do commit correspondente.
