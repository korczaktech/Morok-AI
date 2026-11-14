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
- Retorna relatório, parâmetros candidatos em memória, avaliação dos parâmetros e RequestId. Quando best_model_path é configurado, o treinador também exporta o checkpoint de inferência no formato MOROKCKP existente.
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

O subsistema combina o cancelamento do RequestContext com um callback opcional do chamador e o propaga ao treinador. O pipeline consulta o callback durante a varredura do corpus, antes de cada documento/época, durante blocos BPTT e durante avaliações de validação/teste. Ao cancelar, a execução retorna CANCELLED e o candidato privado não é publicado. Checkpoints e linhas de log gravados antes do cancelamento podem permanecer no disco; retomada continua sujeita à compatibilidade normal do checkpoint.

## Arquivos

- include/morok/learn/learning_subsystem.hpp
- src/learn/learning_subsystem.cpp
- tests/unit/learn/test_learning_subsystem.cpp
- CMakeLists.txt
- tests/CMakeLists.txt

## Testes de aceitação

learn.learning_subsystem cobre lifecycle IA-CORE, treinamento de corpus temporário de três documentos, relatório/estatísticas, preservação dos parâmetros originais, correlação RequestId, cancelamento prévio e durante a varredura/treinamento, política de treinamento desabilitado e shutdown.

## Limitações explícitas

- Sem treinamento online, aprendizagem por conversa, reinforcement learning ou atualização automática em produção.
- Sem scheduler concorrente, fila persistente ou execução em background.
- Sem promoção automática do candidato para IA-LANG.
- Sem validação automática de licença/proveniência do corpus; a autorização é responsabilidade do chamador.
- Cancelamento é cooperativo entre documentos/blocos e leituras; uma operação de sistema de arquivos já bloqueada não pode ser interrompida à força.
- Métricas de qualidade continuam limitadas ao BPB; não demonstram compreensão ou qualidade conversacional.
- Segmentador adaptativo de 29 parâmetros continua fora do caminho B0 de treinamento.

## Critério de conclusão

IA-LEARN está funcionalmente integrado quando executa experimentos G1 por contrato explícito, respeita políticas/limites, mantém parâmetros de inferência isolados, retorna artefato candidato e relatório avaliável, participa do lifecycle IA-CORE e possui teste CTest. A validação final depende de GCC, Clang, Release e sanitizers no commit desta etapa.
