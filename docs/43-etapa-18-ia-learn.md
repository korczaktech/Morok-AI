# Etapa 18 — IA-LEARN: experimentos, treinamento e artefatos

## Objetivo

Implementar o subsistema que coordena experimentos da G1 fora do caminho de inferência. IA-LEARN usa o pipeline de treinamento existente; não implementa um segundo otimizador nem altera o modelo servido durante uma conversa.

## Contrato e fluxo

- Inicialização validada por IA-CORE Runtime e callbacks de lifecycle.
- Política configurável para habilitar/desabilitar treinamento e limitar épocas, documentos e tamanho máximo de documento.
- Execução síncrona e serializada: no máximo um experimento ativo por instância.
- Recebe RequestContext, configuração de experimento, corpus autorizado e parâmetros iniciais.
- Valida limites e parâmetros iniciais finitos antes de iniciar.
- Treina uma cópia privada dos parâmetros; o objeto de parâmetros fornecido pelo chamador permanece inalterado.
- Reutiliza train_g1_corpus, incluindo split determinístico, deduplicação, AdamW/SGD, BPTT, early stopping, métricas, checkpoints e exportação opcional do melhor artefato.
- Revalida finitude e calcula estatísticas dos parâmetros do candidato antes de devolvê-lo.
- Retorna relatório, candidato versionado pelo formato de checkpoint existente, avaliação dos parâmetros e RequestId.
- Erros são retornados como Result<T> com subsistema/estágio/RequestId; falhas de alocação são mapeadas para OUT_OF_MEMORY.
- Treinamento desabilitado é negado por política explícita.

## Segurança e isolamento

- IA-LEARN não é chamado automaticamente por IA-COMM ou IA-LANG.
- Não há atualização online durante inferência.
- O candidato só é devolvido ao chamador; promoção/troca de modelo servido permanece uma operação separada e controlada.
- A execução é serializada com shutdown por mutex.
- O corpus é fornecido explicitamente pelo chamador. O subsistema não baixa dados nem presume licença de uso.
- A identidade da requisição é preservada no resultado e em diagnósticos.

## Cancelamento: limite conhecido do pipeline

O subsistema rejeita requisições já canceladas e descarta o candidato se o cancelamento for observado após o treinador retornar. O pipeline legado train_g1_corpus ainda não recebe token de cancelamento interno; portanto, uma execução longa não é interrompida no meio de uma época. O checkpoint/log já escrito pelo treinador pode permanecer no disco. Cancelamento cooperativo por documento/bloco é uma evolução pendente, não é anunciado como suportado nesta etapa.

## Arquivos

- include/morok/learn/learning_subsystem.hpp
- src/learn/learning_subsystem.cpp
- tests/unit/learn/test_learning_subsystem.cpp
- CMakeLists.txt
- tests/CMakeLists.txt

## Testes de aceitação

learn.learning_subsystem cobre lifecycle IA-CORE, treinamento de corpus temporário de três documentos, relatório/estatísticas, preservação dos parâmetros originais, correlação RequestId, cancelamento prévio, política de treinamento desabilitado e shutdown.

## Limitações explícitas

- Sem treinamento online, aprendizagem por conversa, reinforcement learning ou atualização automática em produção.
- Sem scheduler concorrente, fila persistente ou execução em background.
- Sem promoção automática do candidato para IA-LANG.
- Sem validação automática de licença/proveniência do corpus; a autorização é responsabilidade do chamador.
- Cancelamento não interrompe o pipeline legado durante a execução.
- Métricas de qualidade continuam limitadas ao BPB; não demonstram compreensão ou qualidade conversacional.
- Segmentador adaptativo de 29 parâmetros continua fora do caminho B0 de treinamento.

## Critério de conclusão

IA-LEARN está funcionalmente integrado quando executa experimentos G1 por contrato explícito, respeita políticas/limites, mantém parâmetros de inferência isolados, retorna artefato candidato e relatório avaliável, participa do lifecycle IA-CORE e possui teste CTest. A validação final depende de GCC, Clang, Release e sanitizers no commit desta etapa.
