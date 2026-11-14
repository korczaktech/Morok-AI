# Etapa 13 — IA-CORE e contratos operacionais

## Objetivo

Implementar o núcleo de coordenação do Morok AI sem misturar responsabilidades cognitivas. IA-CORE administra configuração, ciclo de vida, inicialização e encerramento, identidade das requisições, cancelamento, limites de recursos e saúde dos subsistemas.

A etapa não implementa IA-LANG, memória, raciocínio, ferramentas ou comunicação completos. Ela cria contratos executáveis para que esses subsistemas possam ser conectados posteriormente.

## Contratos entregues

### Configuração

`RuntimeConfig` reúne identidade do runtime, versão do contrato, limite de sessões e políticas de recursos. A validação ocorre antes da inicialização de qualquer subsistema.

Os limites iniciais seguem a especificação G1:
- memória residente desejada: 512 MiB;
- limite rígido de memória do processo: 1 GiB;
- entrada máxima por chamada: 1 MiB;
- geração máxima por chamada: 256 bytes;
- concorrência ativa inicial: uma requisição.

Esses valores são políticas e limites declarados. A implementação atual não mede RSS nem instala limites do sistema operacional; o chamador deve consultar as políticas antes de alocar.

### Ciclo de vida

Estados explícitos:
- `created`;
- `initializing`;
- `ready`;
- `stopping`;
- `stopped`;
- `failed`.

O runtime só aceita requisições no estado `ready). A inicialização valida configuração e executa callbacks na ordem fornecida pelo composition root. Em falha, executa rollback dos subsistemas já inicializados em ordem inversa. O encerramento é idempotente após atingir `stopped`.

A ordem de dependências definida no documento 20 continua sendo responsabilidade do composition root: configuração → armazenamento → IA-LANG/modelo → IA-MEM → IA-REAS → IA-TOOLS → IA-COMM. IA-CORE fornece o mecanismo e não instancia subsistemas ainda inexistentes.

### Identidade e contexto de requisição

`RequestContext` associa uma requisição a um `SessionId`, um `RequestId`, instante UTC de criação e token de cancelamento. Os identificadores usam os tipos fortes compartilhados da etapa 2. O contexto não guarda conteúdo de usuário por padrão.

### Cancelamento

`CancellationSource` e `CancellationToken` compartilham um sinal atômico. O cancelamento é seguro entre threads; o trabalho cooperativo deve consultar o token em pontos seguros. Cancelar não interrompe à força código que não consulta o token.

### Limites de recursos

`ResourceLimits` valida consistência e oferece verificações de tamanho de entrada, geração e memória residente. A etapa não configura cgroups, job objects, limites de allocator ou monitoramento RSS; esses mecanismos exigem integração específica por plataforma.

### Saúde

`HealthReport` agrega estado de ciclo de vida e saúde por subsistema:
- falha em dependência obrigatória resulta em `unhealthy`;
- falha em subsistema opcional resulta em `degraded`;
- todos saudáveis com runtime pronto resulta em `healthy`.

A saúde não é inferida por existência de diretórios: cada integração deve publicar seu estado real.

## Arquivos

- `include/morok/core/lifecycle.hpp`
- `include/morok/core/configuration.hpp`
- `include/morok/core/cancellation.hpp`
- `include/morok/core/request_context.hpp`
- `include/morok/core/resource_limits.hpp`
- `include/morok/core/health.hpp`
- `include/morok/core/runtime.hpp`
- `tests/unit/core/test_runtime_contracts.cpp`

## Testes de aceitação

O teste de contratos verifica:
1. validação e rejeição de limites incoerentes;
2. limites de entrada, geração e memória;
3. cancelamento compartilhado e idempotente;
4. geração de RequestId associado a SessionId;
5. classificação de saúde healthy/degraded/unhealthy;
6. transições de inicialização, pronto e encerramento;
7. bloqueio de requisições fora do estado ready;
8. rollback inverso após falha de inicialização;
9. encerramento idempotente.

## Limitações explícitas

- Não existe ainda composition root da aplicação nem servidor.
- A inicialização recebe callbacks; os sete subsistemas não foram todos implementados.
- Não há criação/expiração de sessões nem registro concorrente de requisições.
- O cancelamento é cooperativo.
- Limites são validados e consultáveis, mas não impostos ao sistema operacional.
- Não há coleta de métricas, tracing distribuído ou logger integrado nesta etapa.
- Exceções lançadas por callbacks de subsistema não devem ser usadas como fluxo normal; o contrato público é `Status`. O runtime deve ser cercado por tratamento de falhas no ponto de composição.

## Critério de conclusão

A camada IA-CORE da G1 possui contratos tipados e testáveis para configuração, lifecycle, request context, cancelamento, limites e health, sem violar a separação entre subsistemas. A conclusão de integração end-to-end depende das etapas futuras de composition root, sessões reais, servidor e observabilidade.
