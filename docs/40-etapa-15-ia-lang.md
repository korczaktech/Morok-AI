# Etapa 15 — IA-LANG: adaptador linguístico da G1

## Objetivo e escopo

Implementar a primeira camada IA-LANG executável entre texto UTF-8, sessões do IA-CORE e o gerador autoregressivo G1. Esta etapa fecha o caminho textual mínimo de entrada → contexto isolado → geração → saída UTF-8.

IA-LANG não é um parser semântico nem prova compreensão de linguagem natural. A G1 continua sendo um preditor de bytes com 1.000 parâmetros arquiteturais, dos quais 971 participam do caminho B0 de treinamento. Não são adicionados pesos, embeddings, vocabulário externo, tokenizer aprendido ou modelo pré-treinado.

## Contrato implementado

A classe `LanguageSubsystem` fornece:

- inicialização validada por `RuntimeConfig`;
- cópia imutável dos parâmetros G1 recebidos pelo construtor;
- validação de finitude dos 1.000 parâmetros antes de ativar o subsistema;
- callbacks `initializer()` e `shutdown_callback()` para o lifecycle IA-CORE;
- criação de sessões via `SessionRegistry`;
- contexto textual isolado por `SessionId`;
- observação/acréscimo de texto UTF-8 validado;
- reset explícito do contexto de uma sessão;
- geração greedy por `generate_g1`;
- fechamento coordenado da sessão e descarte do contexto;
- contagem de sessões ativas para diagnóstico.

A implementação está em `include/morok/lang/language_subsystem.hpp` e `src/lang/language_subsystem.cpp`.

## Fluxo de dados

1. IA-CORE cria uma sessão e entrega seu `SessionId`.
2. O chamador cria um `RequestContext` associado à sessão.
3. O texto recebido deve ser convertido em `Utf8Text`; texto inválido é rejeitado antes de entrar no contexto.
4. `observe` verifica inicialização, cancelamento, sessão e limite de entrada.
5. O texto é anexado ao contexto daquela sessão, sem compartilhar estado recorrente com outras sessões.
6. `generate` valida o orçamento e envia o contexto UTF-8 ao gerador G1.
7. O gerador mantém a restrição UTF-8 e o limite de bytes da G1.
8. A continuação validada é anexada ao contexto da sessão e retornada junto com bytes gerados, avaliações de candidatos e indicação de limite.
9. `reset_session` limpa o contexto; `close_session` fecha o registro IA-CORE e remove o estado linguístico.

O modelo recebe somente texto convertido em bytes e parâmetros. Não recebe objetos de conversa, conexões de rede, ferramentas ou dados do banco.

## Limites e segurança

- Contexto por sessão padrão: 64 KiB.
- Entrada por chamada: respeita `ResourceLimits::max_input_bytes`.
- Geração: respeita `ResourceLimits::max_generation_bytes` e o teto de 256 bytes da G1.
- Sessões: respeitam `RuntimeConfig::max_sessions` e a capacidade do `SessionRegistry`.
- Contexto nunca é truncado silenciosamente; ao atingir o limite, a chamada retorna `RESOURCE_LIMIT`.
- Cancelamento é verificado antes e depois da geração. O decoder G1 atual não aceita token de cancelamento interno, portanto não há interrupção no meio da busca de candidatos.
- Parâmetros são copiados para o subsistema e não são modificados durante inferência.
- Sessões são protegidas por mutex; a primeira versão serializa operações de linguagem dentro da instância.
- Encerramento limpa o contexto local. A coordenação do fechamento do `SessionRegistry` deve ocorrer por `close_session`; não fechar sessões diretamente fora do adaptador se houver contexto linguístico ativo.

## Erros tipados

As operações retornam `Result<T>` ou `Status`, com erros de argumento inválido, UTF-8 inválido, entrada excessiva, limite de recurso, subsistema/modelo indisponível, cancelamento, falha de alocação e erro interno.

A validação UTF-8 estrita existente em `Utf8Text` rejeita sequências inválidas. A conversão texto↔bytes é reversível, pois o modelo consome os bytes UTF-8 originais sem normalização Unicode.

## Testes

O teste `lang.language_subsystem` cobre:
1. inicialização e encerramento pelo lifecycle IA-CORE;
2. criação de sessões e respeito à capacidade;
3. associação de RequestContext à sessão correta;
4. ingestão de texto UTF-8;
5. geração com limite configurado;
6. validação UTF-8 da continuação;
7. isolamento entre sessões;
8. rejeição de geração acima do limite;
9. cancelamento antes da geração;
10. reset e fechamento de sessão;
11. rejeição de contexto de sessão encerrada;
12. rejeição de operações após shutdown.

Arquivo: `tests/unit/lang/test_language_subsystem.cpp`.

## Limitações explícitas

- Não há identificação de idioma; não existe classificador avaliado.
- Não há tokenizer, vocabulário ou segmentador linguístico aprendido; a representação de entrada continua byte-level UTF-8.
- Não há normalização, correção gramatical, tradução, classificação semântica ou parser de intenção.
- Não há streaming de tokens/bytes; a chamada retorna a continuação completa.
- A memória de conversa é transitória e limitada à sessão. Persistência de fatos e recuperação continuam sob responsabilidade do IA-MEM e exigem integração explícita do composition root.
- O cancelamento não interrompe a rotina de decodificação internamente.
- A qualidade linguística da G1 não está validada. O gerador pode produzir continuações sem coerência.
- A implementação não instala limites de memória do sistema operacional; aplica limites lógicos antes das operações.
- A política atual serializa chamadas por instância; concorrência e throughput precisam de benchmark próprio.

## Critério de conclusão

IA-LANG G1 possui adaptador compilável e testável para texto UTF-8, sessões IA-CORE, contexto isolado, geração G1, limites, erros tipados e callbacks de lifecycle. O critério cobre integração funcional do adaptador, não qualidade semântica ou conversacional do modelo.
