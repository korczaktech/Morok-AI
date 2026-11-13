# 21. Persistência, memória e formatos de dados

## 21.1 Decisões de armazenamento

- Parâmetros do modelo: arquivo binário versionado, conforme documento 18.
- Configurações: TOML, com validação de esquema e valores padrão explícitos.
- Metadados e memória: SQLite.
- Busca lexical: SQLite FTS5 quando disponível no build; caso contrário, build deve falhar ou declarar modo sem busca, nunca simular FTS.
- Arquivos grandes: armazenamento em diretório de objetos com nomes derivados de hash e metadados no SQLite.
- Logs: JSON Lines rotacionados, sem conteúdo sensível por padrão.

## 21.2 Organização lógica da memória

Tabelas conceituais:
- memories: id, type, title, body_ref, source, created_at, updated_at, sensitivity, retention_policy;
- memory_tags: memory_id, tag;
- memory_links: source_id, target_id, relation;
- sessions: id, created_at, expires_at, status;
- audit_events: id, request_id, action, actor, timestamp, outcome.

Índices são obrigatórios para IDs, timestamps, tipo e chaves de relacionamento. FTS5 indexa apenas campos textuais permitidos pela política de privacidade.

## 21.3 Transações e concorrência

Usar transações SQLite para alterações relacionadas. WAL habilitado para permitir leitores concorrentes com um escritor. O primeiro release mantém um escritor lógico por processo. Não assumir que múltiplos processos podem escrever simultaneamente sem coordenação.

## 21.4 Retenção e exclusão

- Memória de trabalho expira ao encerrar sessão, salvo promoção explícita.
- Memória persistente segue política por tipo e sensibilidade.
- Exclusão remove o registro, seus índices e referências; objetos compartilhados só são apagados quando não houver referências.
- Backups devem refletir exclusões em sua próxima rotação.
- Exportação deve preservar proveniência e política de retenção.

## 21.5 Proveniência e confiança

Cada memória persistente registra de onde veio: usuário, ferramenta, documento, inferência ou sistema. Conteúdo inferido não pode ser armazenado como fato confirmado sem rótulo de inferência e evidência. Recuperação retorna fonte e data.

## 21.6 Limites iniciais

- Tamanho máximo de uma entrada de memória: 64 KiB.
- Resultado máximo de uma busca: 20 registros.
- Conteúdo máximo agregado enviado ao modelo por recuperação: 4 KiB na G1.
- Timeout inicial de busca local: 100 ms; se excedido, retornar timeout e não bloquear a conversa.
- Cache em memória limitado e descartável; banco persistente é fonte de verdade.

Esses limites são configuráveis e devem ser medidos em hardware de referência.

## 21.7 Integridade e backup

- Verificar integridade SQLite em rotina de manutenção.
- Backup consistente via API de backup SQLite ou snapshot transacional.
- Checksum para objetos.
- Migrações de esquema numeradas e transacionais.
- Nunca sobrescrever banco antigo antes de backup e validação de migração.
