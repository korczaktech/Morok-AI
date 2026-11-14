# Etapa 14 — IA-MEM: memória persistente e recuperação

## Objetivo

Entregar o subsistema IA-MEM da G1 com armazenamento local, persistência transacional, isolamento por sessão, proveniência, busca textual, retenção e exclusão auditável. A implementação usa SQLite em C++20. Não usa embeddings, vetores ou modelos externos/pré-treinados.

## Tipos de memória

- **Trabalho:** estado transitório associado à sessão. É removido por `clear_working(session_id)` no encerramento da sessão, a menos que seja promovido antes.
- **Episódica:** registros persistentes de eventos e experiências.
- **Semântica:** registros persistentes de conhecimento explicitamente armazenado.

A promoção de memória de trabalho para episódica ou semântica é explícita e transacional. A memória não é promovida automaticamente por inferência.

## Persistência e esquema

O banco usa SQLite com:
- WAL e synchronous=FULL;
- conexão FULLMUTEX e busy timeout;
- migração inicial numerada em transação;
- tabelas `memories`, `memory_audit` e `schema_migrations`;
- índices por sessão/tipo e expiração;
- FTS5 com triggers para manter o índice sincronizado quando disponível;
- fallback de busca literal com LIKE quando a biblioteca SQLite não oferece FTS5.

A memória persistida é a fonte de verdade. Não existe cache obrigatório.

## Contrato de cada registro

Cada registro contém:
- MemoryId UUID opaco;
- SessionId;
- tipo de memória;
- título e corpo UTF-8 estritos;
- origem: usuário, ferramenta, documento, inferência ou sistema;
- identificador da fonte;
- nível de sensibilidade;
- instante de criação;
- expiração opcional.

O armazenamento exige identificador de proveniência não vazio. A recuperação retorna o conteúdo junto com a origem, fonte, tipo, sensibilidade e timestamps.

## Privacidade e isolamento

Todas as operações de recuperação e busca exigem SessionId e filtram no próprio SQL por essa sessão. Conhecer um MemoryId não permite recuperar ou excluir uma memória de outra sessão.

Registros marcados como sensíveis não entram no índice FTS5 e não são retornados pela busca textual. A recuperação direta por ID permanece limitada à sessão proprietária. Logs de auditoria armazenam metadados da operação, não o corpo da memória.

## Busca e limites

Padrões iniciais:
- entrada total por registro (título + corpo + source_id): 64 KiB;
- resultados por busca: até 20;
- contexto agregado por busca: até 4 KiB;
- timeout de bloqueio SQLite: 100 ms;
- orçamento de execução da busca: 100 ms;
- limite de banco: 256 MiB, aplicado por max_page_count com páginas de 4 KiB.

O limite de resultados/contexto também é aplicado na camada C++, independentemente do plano de consulta. A busca FTS trata a consulta como termos literais AND, sem permitir que o texto do usuário injete operadores FTS. A consulta aceita `CancellationToken` e respeita cancelamento cooperativo.

## Retenção, promoção e exclusão

- Memória de trabalho é limpa por sessão via `clear_working`.
- Memórias episódicas e semânticas podem ter `expires_at`.
- `prune_expired` apaga registros vencidos e registra cada expiração.
- `update_metadata` permite ajustar sensibilidade e expiração; a mudança é auditada e atualiza o índice de busca.
- `promote` move memória de trabalho para episódica ou semântica e registra o evento.
- `erase` exige ator e SessionId, registra evento de exclusão na mesma transação que remove o registro.
- Triggers FTS removem o conteúdo correspondente do índice quando o registro é apagado.

O encerramento de uma sessão deve usar `MemorySubsystem::close_session(registry, session_id)`: o adaptador limpa a memória de trabalho e só então remove a sessão do `SessionRegistry`. O composition root deve registrar `initializer()` e `shutdown_callback()` no Runtime.

## Backup e integridade

`backup_to` usa a API SQLite Backup para criar snapshot consistente do banco aberto, incluindo dados e auditoria. `integrity_check` executa `PRAGMA integrity_check` e só retorna sucesso quando SQLite responde `ok`.

O arquivo de backup contém dados persistentes e deve receber as mesmas permissões e controles de acesso do banco original.

## Arquivos

- `include/morok/memory/memory_store.hpp`
- `include/morok/memory/memory_subsystem.hpp`: adaptador de lifecycle IA-CORE e encerramento de sessão
- `src/memory/memory_store.cpp`
- `tests/unit/memory/test_memory_store.cpp`
- `CMakeLists.txt`: dependência SQLite3 e inclusão da implementação
- `tests/CMakeLists.txt`: teste `memory.store`

## Testes de aceitação

O teste automatizado cobre:
1. abertura e fechamento do banco;
2. armazenamento e recuperação com proveniência;
3. parsing e round-trip de MemoryId;
4. busca textual e limite de contexto;
5. isolamento entre duas sessões;
6. ocultação de memórias sensíveis na busca;
7. promoção de memória de trabalho;
8. limpeza de memória de trabalho sem remover memórias promovidas;
9. expiração e pruning;
10. exclusão e evento de auditoria persistente;
11. integridade SQLite;
12. backup consistente;
13. inicialização do IA-MEM com RuntimeConfig e limpeza da memória de trabalho antes de encerrar a sessão no SessionRegistry.

## Limitações explícitas

- O adaptador `MemorySubsystem` já conecta a inicialização ao `RuntimeConfig` e oferece `close_session`, que limpa memória de trabalho antes de fechar o `SessionRegistry`; o composition root ainda precisa registrar esse adaptador como callback de inicialização/encerramento do Runtime.
- Não há embeddings nem busca semântica vetorial; a memória semântica é uma classificação de registros explícitos, pesquisável lexicalmente.
- O FTS5 depende da build de SQLite; quando ausente, a implementação declara `fts5_available() == false` e usa busca literal LIKE.
- O backup é exportação de snapshot; restauração operacional e rotação automática ainda não fazem parte desta etapa.
- Criptografia em repouso não é alegada nem implementada.
- Limites padrão precisam ser medidos em hardware real de 4 GB.

## Critério de conclusão

IA-MEM G1 possui armazenamento SQLite transacional, três tipos de memória, promoção explícita, busca textual limitada, isolamento por sessão, proveniência, retenção, exclusão auditável, backup e verificação de integridade, com teste automatizado registrado no CTest, incluindo o adaptador de sessão IA-CORE. A etapa não afirma integração automática com um servidor ou composition root ainda inexistente.
