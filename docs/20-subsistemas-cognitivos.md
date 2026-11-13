# 20. Especificação funcional dos sete subsistemas

**Estado:** responsabilidades, limites e comportamento mínimo definidos. Capacidades avançadas dependem de implementação e testes.

## 20.1 IA-CORE — orquestração

Responsabilidades:
- carregar configuração validada;
- inicializar subsistemas na ordem de dependência;
- verificar saúde e compatibilidade de versões;
- controlar ciclo de vida e encerramento;
- distribuir RequestId e SessionId;
- propagar cancelamento e limites;
- coordenar logs e diagnóstico.

Ordem de inicialização: configuração → armazenamento → IA-LANG/modelo → IA-MEM → IA-REAS → IA-TOOLS → IA-COMM. Falha em dependência obrigatória impede inicialização; dependências opcionais podem operar desativadas com estado explícito.

IA-CORE não implementa raciocínio, linguagem ou persistência internamente.

## 20.2 IA-LANG — linguagem

Responsabilidades:
- validação UTF-8 estrita;
- conversão reversível texto↔bytes;
- segmentação RLAH;
- interface do modelo;
- geração e validação da saída;
- identificação de idioma apenas quando houver módulo implementado e avaliado.

A G1 implementa bytes e segmentação auxiliar. Não se presume compreensão semântica, tradução, classificação ou qualidade conversacional.

## 20.3 IA-MEM — memória

A memória é externa aos parâmetros treináveis. Tipos iniciais:
- memória episódica: eventos e interações com origem e data;
- memória semântica: fatos explicitamente extraídos e associados a fonte;
- memória de trabalho: contexto temporário da tarefa.

Cada registro possui ID, tipo, conteúdo, origem, criação, atualização, classificação de sensibilidade, política de retenção e proveniência.

Recuperação G1: busca lexical com SQLite FTS5/BM25, filtros por metadados e recência como sinal secundário. Não usar embeddings semânticos até existir encoder próprio avaliado. Resultados retornam pontuação e fonte; não são automaticamente considerados verdadeiros.

Memória de trabalho expira ao encerrar a sessão, salvo persistência explícita. Memória de longo prazo exige política de retenção e operação de exclusão auditável.

## 20.4 IA-REAS — raciocínio e planejamento

Primeira versão: planejador simbólico determinístico sobre tarefas estruturadas. Não será anunciada como raciocínio geral.

Representação de tarefa:
- objetivo;
- entradas e restrições;
- etapas tipadas;
- dependências;
- pré-condições;
- pós-condições;
- orçamento de passos;
- política de falha.

Limites iniciais: 32 etapas por plano, profundidade de dependência 8 e uma tentativa de recuperação por etapa. Valores são limites de segurança configuráveis. O planejador deve detectar ciclos e dependências impossíveis antes da execução.

Operações matemáticas e lógicas devem ser executadas por funções determinísticas testadas, não por texto gerado pelo modelo.

## 20.5 IA-LEARN — aprendizado

Responsabilidades:
- criar e executar experimentos;
- carregar corpora autorizados;
- treinar sem pesos pré-treinados;
- avaliar e comparar;
- salvar checkpoints;
- gerar relatórios;
- promover artefatos aprovados.

Treinamento não ocorre automaticamente durante conversa. Aprendizado online fica desativado na primeira versão para evitar corrupção de parâmetros, regressões e vazamento entre sessões.

## 20.6 IA-COMM — comunicação

Responsabilidades:
- protocolo de mensagens;
- sessões;
- validação de tamanho e codificação;
- formatação de resposta;
- streaming quando suportado;
- apresentação de erros e estados de ferramenta.

Papéis internos: system, user, assistant e tool. Mensagens de ferramenta preservam o nome da ferramenta e RequestId. IA-COMM não deve transformar saída inválida em texto válido silenciosamente.

## 20.7 IA-TOOLS — ferramentas

Responsabilidades:
- registro e descoberta de ferramentas;
- validação de esquema;
- autorização;
- confirmação de efeitos colaterais;
- execução isolada;
- timeout e limite de saída;
- auditoria.

Ferramentas são desativadas por padrão até serem registradas e autorizadas. Nenhuma ferramenta recebe acesso irrestrito ao sistema operacional.

## 20.8 Dependências entre subsistemas

- IA-CORE coordena todos.
- IA-COMM recebe solicitações e apresenta respostas.
- IA-LANG processa texto e modelo.
- IA-MEM fornece contexto recuperado sob política.
- IA-REAS produz planos estruturados.
- IA-TOOLS executa ações permitidas.
- IA-LEARN cria versões do modelo fora da inferência.

Dependências são unidirecionais por contrato; evitar ciclos de chamadas entre módulos. A orquestração de fluxos fica em IA-CORE.

## 20.9 Critério de maturidade

Cada subsistema evolui por estados: especificado, implementado, testado, validado. A presença de diretório, classe ou interface não comprova capacidade funcional.
