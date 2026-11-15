# Etapa 2 — Tipos compartilhados e contratos básicos

**Status:** implementação integrada; GCC/Clang em Debug/Release aprovados no CI.

## Tipos implementados

- `ByteBuffer`: vetor de bytes arbitrários; não implica texto.
- `Utf8Text`: texto proprietário que só pode ser construído após validação UTF-8 estrita.
- `SessionId`, `MemoryId`, `RequestId`: tipos fortes distintos, com 128 bits e geração por fonte aleatória da plataforma. A geração retorna erro se a fonte não estiver disponível.
- `ModelVersion`: identidade imutável de arquitetura, quantidade de parâmetros e versão do formato, criada por fábrica validada.
- `Timestamp`: instante UTC em nanossegundos desde Unix epoch.
- `ErrorCode`: catálogo tipado dos códigos previstos no contrato.
- `Error`: código, contexto de subsistema/etapa, mensagem técnica e RequestId opcional.
- `Result<T>`: valor ou erro, sem estado parcialmente válido e sem exceções para fluxo normal.
- `Status`: alias de `Result<void>`.
- `types.hpp`: cabeçalho agregador para os contratos compartilhados.

## UTF-8

O validador rejeita bytes de continuação isolados, sequências truncadas, codificações overlong, surrogates, valores acima de U+10FFFF e bytes iniciais inválidos. U+0000 é válido e não encerra o texto; o tamanho é sempre medido em bytes.

## Identificadores

Os identificadores são tipos incompatíveis entre si em tempo de compilação. A representação textual é UUID compatível, mas o contrato os trata como opacos. A implementação usa `std::random_device`; a qualidade da fonte depende da implementação da plataforma. A geração falha explicitamente se a fonte reportar ausência de entropia ou lançar exceção. Para ambientes de produção, a fonte do sistema operacional deve ser auditada por plataforma.

## Erros

Códigos estáveis em maiúsculas são disponibilizados por `to_string(ErrorCode)`. Mensagens são diagnósticas e não devem conter segredos nem conteúdo sensível. O RequestId é opcional durante a construção do erro e pode ser anexado pela camada de orquestração.

## Testes

Os testes cobrem:
- bytes arbitrários versus texto validado;
- UTF-8 ASCII, multibyte, NUL e texto vazio;
- UTF-8 inválido (continuação isolada, overlong, surrogate, fora do Unicode e truncamento);
- sucesso e falha em Result/Status;
- códigos de erro e contexto;
- distinção dos tipos fortes e geração de IDs;
- validação e imutabilidade de ModelVersion;
- representação e relógio de Timestamp.

## Limites desta etapa

Esta etapa não implementa cancelamento, sessão de inferência, armazenamento, serialização de IDs, logging ou contratos completos de subsistemas. Esses componentes dependem dos tipos básicos e serão implementados nas etapas próprias.
