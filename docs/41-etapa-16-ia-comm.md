# Etapa 16 — IA-COMM: comunicação, conversas e mensagens

## Objetivo

Implementar a camada de comunicação da Morok AI G1 sobre IA-CORE e IA-LANG. IA-COMM controla conversas, mensagens, correlação por requisição, histórico limitado, apresentação de erros e entrega de respostas em eventos de streaming. Não contém treinamento, raciocínio ou execução de ferramentas.

## Arquitetura

- Message: identificador opaco próprio, ConversationId, SessionId, RequestId, papel, conteúdo UTF-8, instante, estado e nome da ferramenta quando o papel for tool.
- Conversation: identificador próprio, sessão IA-CORE/IA-LANG, timestamps, histórico ordenado e estado de fechamento.
- CommunicationSubsystem: cria e fecha conversas, cria RequestContext associado à conversa, aceita mensagens de controle, recebe mensagens do usuário, chama IA-LANG, mantém histórico e entrega eventos.
- MessageFormatter: formata mensagens individuais, transcrições com rótulos de papel e erros tipados sem transformar conteúdo inválido silenciosamente.
- StreamEvent: eventos message_started, text_delta, message_completed, message_cancelled e error.
- StreamCallback: consumidor síncrono dos eventos; falhas do consumidor são propagadas como Status.

IDs de mensagem e conversa são tipos distintos (MessageId e ConversationId), gerados pelo mesmo mecanismo UUID opaco do projeto. RequestId é preservado em cada mensagem para rastreabilidade.

## Fluxo de uma conversa

1. IA-COMM cria uma sessão por IA-LANG/IA-CORE e associa um ConversationId.
2. O chamador obtém um RequestContext vinculado à conversa; o chamador pode fornecer CancellationToken.
3. Mensagens system e tool podem ser adicionadas como mensagens de controle. Exigem RequestContext da mesma sessão; mensagens tool exigem nome não vazio e mensagens system não aceitam nome de ferramenta.
4. Mensagem user é validada como Utf8Text, limitada em bytes e registrada no histórico.
5. IA-COMM encaminha a transcrição com rótulos de papel a IA-LANG::observe, adiciona o marcador Assistant e solicita IA-LANG::generate.
6. A resposta assistant recebe MessageId e RequestId próprios, é entregue pelos eventos e registrada no histórico.
7. Encerrar a conversa fecha a sessão IA-LANG/IA-CORE. O encerramento do Runtime fecha conversas antes de encerrar IA-LANG, respeitando a ordem inversa das dependências.

A G1 não possui token EOS. O tamanho de geração continua limitado pelo contrato IA-LANG/G1. IA-COMM não altera a saída do modelo nem promete coerência linguística.

## Streaming

A API expõe eventos de início, fragmentos de texto, conclusão, cancelamento e erro. Os fragmentos são cortados em fronteiras UTF-8, nunca no meio de uma sequência multibyte. A sequência dos fragmentos é numerada a partir de zero.

**Limitação importante:** a geração G1 atual retorna a continuação completa antes da entrega dos eventos. Portanto, o streaming desta etapa é de transporte/apresentação em fragmentos após a geração, não decodificação token-a-token nem geração incremental de baixa latência. O callback é síncrono e sua falha interrompe a entrega. Cancelamento é verificado antes da geração e entre fragmentos; não interrompe o decoder G1 durante a busca de candidatos.

## Concorrência e isolamento

- Uma requisição ativa por conversa; chamadas concorrentes na mesma conversa são rejeitadas.
- Conversas diferentes possuem sessões e contextos IA-LANG distintos.
- O mapa de conversas/histórico é protegido por mutex.
- Callbacks de streaming são chamados sem manter o mutex de histórico.
- Fechamento de conversa com requisição ativa é rejeitado.
- RequestContext de outra sessão não pode acessar conversa.
- A sessão IA-LANG é fechada junto com a conversa.

## Limites iniciais

| Limite | Padrão |
|---|---:|
| Conversas ativas | 128, limitado por RuntimeConfig::max_sessions |
| Mensagens por conversa | 256 |
| Bytes por mensagem | 64 KiB |
| Bytes de histórico por conversa | 256 KiB |
| Fragmento de streaming | 64 bytes, ajustado a fronteiras UTF-8 |

Os limites são configuráveis via ConversationLimits. O histórico não é truncado nem removido silenciosamente: quando excedido, a operação retorna RESOURCE_LIMIT. O histórico desta etapa é mantido em memória e não sobrevive ao encerramento do processo.

## Erros

Erros de configuração, sessão/conversa inexistente, sessão incompatível, limite de mensagem/histórico, cancelamento, falha de IA-LANG, falha de callback e alocação são retornados como Status/Result<T> com ErrorCode, subsistema e estágio. Falhas de geração podem emitir evento error com formatação textual, além de retornar o erro tipado ao chamador.

## Arquivos

- include/morok/comm/message.hpp
- include/morok/comm/conversation.hpp
- include/morok/comm/formatter.hpp
- include/morok/comm/event_stream.hpp
- include/morok/comm/session_manager.hpp
- src/comm/communication_subsystem.cpp
- tests/unit/comm/test_communication_subsystem.cpp
- include/morok/common/ids.hpp e src/common/ids.cpp: IDs tipados de mensagem e conversa
- CMakeLists.txt e tests/CMakeLists.txt: compilação e CTest

## Testes de aceitação

O teste comm.communication_subsystem cobre:
1. inicialização de IA-LANG antes de IA-COMM e encerramento na ordem inversa;
2. criação de conversa e sessão;
3. criação de RequestContext associado à conversa;
4. papéis system, tool, user e assistant;
5. validação do nome da ferramenta;
6. envio de texto UTF-8 e geração IA-LANG;
7. correlação por RequestId e identificadores distintos;
8. eventos de streaming e fragmentos UTF-8 válidos;\n9. formatação de transcrição e erros;
10. histórico ordenado;
11. cancelamento antes e durante a entrega;
12. rejeição de papel inválido e falha do consumidor de streaming;
13. fechamento da conversa e sessão;
14. rejeição de histórico após fechamento.

## Fora do escopo / limitações explícitas

- Transporte HTTP, WebSocket, TCP, CLI interativa e interface gráfica.
- Persistência de conversas em IA-MEM; o histórico é volátil.
- Decodificação G1 incremental/token-a-token e interrupção interna do decoder.
- Entrega assíncrona, filas, backpressure e reconexão de clientes.
- Execução de ferramentas; IA-COMM apenas representa mensagens com papel tool. IA-TOOLS é etapa separada.
- Moderação, autenticação/autorização de usuários externos e criptografia de transporte; serão tratadas na etapa de segurança/integração.
- Não há garantia de qualidade semântica da resposta G1.

## Critério de conclusão

IA-COMM está funcionalmente implementada quando cria e encerra conversas integradas às sessões IA-CORE/IA-LANG, correlaciona mensagens por IDs, preserva papéis e histórico dentro de limites, encaminha entrada e geração, apresenta erros tipados e entrega eventos de resposta em fronteiras UTF-8, com teste automatizado no CTest. Este critério não declara transporte de rede, persistência de histórico ou streaming incremental.
