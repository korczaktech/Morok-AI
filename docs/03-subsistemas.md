# 3. Sete subsistemas cognitivos

Os sete subsistemas representam responsabilidades de coordenação e produto, não necessariamente sete redes neurais. A especificação funcional e os limites mínimos estão no documento 20.

## 3.1 IA-CORE — Núcleo

Inicialização, encerramento, ciclo de vida, orquestração, configuração, tratamento de falhas, observabilidade e estado global.

## 3.2 IA-LANG — Linguagem

Entrada e saída linguística, UTF-8, representação RLAH, codificação, interpretação e integração com o modelo.

## 3.3 IA-MEM — Memória

Memória de trabalho, episódica e semântica; persistência, indexação, recuperação, retenção e proveniência. Memória externa não é parâmetro treinável.

## 3.4 IA-REAS — Raciocínio

Operações lógicas, resolução estruturada, decomposição, planejamento, avaliação de alternativas e justificativas operacionais. Capacidade geral não é presumida.

## 3.5 IA-LEARN — Aprendizado

Treinamento, atualização de parâmetros, avaliação, experimentos, otimização, checkpoints e reprodutibilidade. Treinamento não modifica pesos em serviço.

## 3.6 IA-COMM — Comunicação

Interface, mensagens, sessões, formatação, streaming e apresentação de erros.

## 3.7 IA-TOOLS — Ferramentas

Registro, esquemas, permissões, execução controlada, isolamento, auditoria e tratamento de erros externos.

## 3.8 Regras de integração

- IA-CORE coordena, sem absorver lógica dos módulos.
- IA-LANG encapsula representação.
- IA-MEM mantém persistência separada do estado transitório.
- IA-REAS produz planos estruturados.
- IA-LEARN não modifica parâmetros durante inferência comum.
- IA-TOOLS opera com permissões explícitas.
- IA-COMM não contém lógica de treinamento.
- Dependências entre módulos seguem contratos unidirecionais.

## 3.9 Critérios de validação

Cada subsistema terá testes unitários, integração e critérios de maturidade separados. Implementação não implica validação.


## 3.10 Camada transversal de segurança

A segurança é transversal aos sete subsistemas e não constitui um oitavo subsistema cognitivo. A implementação inicial reside em `morok::security::SecuritySubsystem`: grants explícitos por sessão/ferramenta/capacidade, política de negação por padrão, confirmações humanas de uso único e classificação de origem de conteúdo. IA-TOOLS consome a política gerada pelo host. Consulte [Segurança e permissões](22-seguranca-e-permissoes.md) e [Etapa 13 — Segurança](46-etapa-13-seguranca.md).
