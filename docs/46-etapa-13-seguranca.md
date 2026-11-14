# Etapa 13 — Segurança

**Estado:** camada inicial de segurança implementada e integrada ao build/CTest. A validação de CI é acompanhada separadamente; implementação não equivale a auditoria externa ou certificação.

## Controles implementados

- Política de privilégio mínimo e negação por padrão.
- Identidade de principal vinculada a SessionId.
- Grants explícitos por par exato (ferramenta + capacidade), sem permissões globais implícitas.
- Revogação de grants e isolamento entre sessões.
- Geração de ToolAccessPolicy a partir de grants mantidos pelo host.
- Confirmações humanas com ticket opaco, prazo de validade, vínculo ao principal/sessão/ação exata e consumo único.
- Auditoria de concessão, revogação, negação e ciclo de confirmação; metadados sem argumentos, destinos completos ou conteúdo sensível.
- Limites de quantidade de grants e confirmações pendentes.
- Classificação imutável de conteúdo por origem: entrada do usuário, documento externo, memória recuperada, saída do modelo e saída de ferramenta. Todos esses rótulos são sempre não confiáveis; não existe API pública para um conteúdo se declarar configuração confiável.
- Configuração confiável permanece fora do fluxo de conteúdo e só pode vir do host após validação e controle de acesso.
- Integração ao lifecycle do IA-CORE por initializer/shutdown callback.

## Autorização de ferramentas

A camada de segurança mantém grants individuais com sujeito, sessão, nome da ferramenta e capacidade. A função `tool_policy` produz uma política com pares exatos. Quando `exact_grants` está preenchido, IA-TOOLS ignora a combinação cartesiana dos vetores legados e exige correspondência simultânea de nome e capacidade.

O composition root deve obter a política por `SecuritySubsystem::tool_policy` e passá-la ao IA-TOOLS. Não aceitar políticas construídas a partir de texto do modelo, documentos, memória ou argumentos do usuário. A aplicação deve impedir que componentes não confiáveis chamem diretamente APIs de concessão/revogação.

## Confirmação humana

O fluxo é:
1. O host define ferramenta, operação, destino e resumo do efeito.
2. `request_confirmation` emite ticket de curta duração.
3. A interface apresenta a ação concreta ao usuário, sem permitir que texto gerado pelo modelo altere os campos.
4. Somente o controlador confiável da interface chama `approve_confirmation` após uma ação humana explícita.
5. `consume_confirmation` valida principal, sessão, ticket, expiração e igualdade exata da ação; o ticket é consumido uma única vez.
6. O host só então sinaliza a confirmação ao executor de ferramenta.

Confirmações não são reutilizáveis para outro destino, operação, ferramenta, sessão ou usuário. Uma aprovação não concede capacidade: autorização e confirmação são verificações independentes.

## Fronteiras de confiança e prompt injection

`classify_content` preserva a origem dos dados. Somente conteúdo explicitamente classificado como configuração confiável pode ser considerado fonte de política. Texto do usuário, documentos, páginas externas, memória, saída do modelo e saída de ferramentas são dados não confiáveis.

A classificação não neutraliza prompt injection por si só. O orquestrador deve manter instruções e dados separados, não executar comandos encontrados em conteúdo externo e não permitir que esses conteúdos alterem grants, configuração ou confirmações.

## Auditoria e privacidade

O audit sink é obrigatório por padrão. Os eventos registram sujeito, sessão, ferramenta, operação, tipo e timestamp. Não registram argumentos, saída da ferramenta, destino detalhado, conteúdo de memória ou segredos. O host é responsável por armazenamento durável, controle de acesso, retenção e proteção contra adulteração.

## Limites que permanecem explícitos

- Não existe sandbox de processo/OS para handlers; IA-TOOLS continua executando handlers C++ confiáveis no processo principal.
- Não há isolamento por namespace, seccomp, job objects, limites reais de CPU/RAM por ferramenta ou bloqueio de rede no nível do SO.
- A camada não implementa criptografia em repouso nem gestão/rotação de chaves.
- A classificação de origem depende de o host atribuir rótulos corretos; não é uma prova criptográfica de proveniência.
- A camada não detecta semanticamente todos os ataques de prompt injection.
- O host ainda precisa compor SecuritySubsystem, IA-TOOLS, UI de confirmação e política de sessão; não há um composition root de produção completo neste passo.

Esses controles não devem ser anunciados como sandbox, proteção criptográfica ou certificação de segurança.

## Testes

O teste `security.security_subsystem` cobre lifecycle, negação por padrão, grants por sessão, geração de política, prevenção de escalada por combinação de capacidades, revogação, classificação de origem, confirmação humana, vínculo à ação exata, vínculo à sessão, rejeição, consumo único e auditoria.
