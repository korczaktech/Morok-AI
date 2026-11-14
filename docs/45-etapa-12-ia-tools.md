# Etapa 12 — IA-TOOLS

**Estado de implementação:** primeira versão funcional integrada ao build e ao CTest.  
**Escopo:** registro, descoberta, contratos de argumentos, autorização por capacidade, confirmação explícita, execução controlada, limites, auditoria e lifecycle IA-CORE.

## Componentes

- `include/morok/tools/tool_subsystem.hpp`: contratos públicos.
- `src/tools/tool_subsystem.cpp`: registro, validação, autorização, execução e auditoria.
- `tests/unit/tools/test_tool_subsystem.cpp`: testes de contrato e comportamento.
- O alvo `morok_core` compila IA-TOOLS; o teste `tools.tool_subsystem` é registrado no CTest.

## Contrato de ferramenta

Cada ferramenta registrada declara nome estável, descrição, esquema tipado de argumentos, capacidade exigida, necessidade de confirmação, timeout, limites de entrada/saída e handler síncrono. Nomes e campos são validados; campos duplicados, nomes inválidos, handlers vazios e limites inconsistentes são rejeitados.

Tipos de argumentos suportados: texto UTF-8, número finito e booleano. Campos obrigatórios são exigidos, tipos são verificados, campos desconhecidos são rejeitados por padrão e strings têm limite individual. Não há interpretação de comandos livres nem execução automática de texto produzido pelo modelo.

## Autorização e confirmação

A descoberta e a execução exigem simultaneamente:
1. nome presente na allowlist da requisição;
2. capacidade correspondente concedida à requisição.

Capacidades: read_only, filesystem_read, filesystem_write, network e external_write. Ferramentas marcadas como efeitos colaterais exigem confirmação explícita no momento da invocação. A confirmação não concede por si só uma capacidade ausente.

A política é fornecida pelo chamador confiável (composition root/IA-CORE). Não se deve aceitar uma política criada pelo texto do usuário ou pela saída do modelo.

## Execução e recursos

Handlers recebem somente argumentos validados e um contexto contendo SessionId, RequestId, consulta de cancelamento e deadline. A execução ocorre fora do mutex do registro. Há limite global de concorrência, timeout máximo, tamanho máximo de entrada e saída, tratamento de exceções e verificação de UTF-8 da saída.

**Limite de segurança da versão:** o handler é código C++ confiável carregado no mesmo processo. O deadline e o cancelamento são cooperativos: o handler precisa consultá-los; o subsistema detecta também uma execução que retorna depois do deadline, mas não consegue interromper com segurança uma função C++ travada. Esta versão não fornece isolamento de processo/OS, syscall filtering, rede em namespace ou sandbox de filesystem. Não registrar código de terceiros não confiável como handler. Para executar ferramentas externas não confiáveis, será necessário um backend separado de processo/sandbox com kill real e políticas do sistema operacional.

## Auditoria

Toda invocação autorizada registra início e resultado; negações, falta de confirmação, cancelamento e timeout também geram eventos. Os registros contêm nome da ferramenta, SessionId, RequestId, timestamp, tipo e detalhe operacional, mas não copiam argumentos ou saída, evitando persistir segredos por padrão.

Por padrão, a execução é bloqueada se não houver audit sink. O sink é injetado pelo host e deve ser durável, protegido contra adulteração e adequado à política de retenção do produto. O subsistema não finge oferecer persistência quando o host fornece apenas um sink em memória.

## Integração IA-CORE

`initializer()` e `shutdown_callback()` permitem registrar IA-TOOLS no `core::Runtime`, com rollback de inicialização e encerramento coordenado. O composition root deve construir o sink e as políticas confiáveis e registrar os callbacks na ordem definida pela arquitetura.

## Testes

O teste automatizado cobre lifecycle, registro duplicado, descoberta filtrada, autorização, argumentos obrigatórios e tipos, campos desconhecidos, limites de texto, confirmação explícita, sucesso, timeout cooperativo, exceção do handler, limite de saída, cancelamento e trilha de auditoria.

## O que esta etapa não afirma

IA-TOOLS não interpreta linguagem natural, não concede permissões por conta própria, não oferece sandbox de sistema operacional e não torna seguro código de handler não confiável. O modelo não pode inventar grants: a autorização deve vir do host.
