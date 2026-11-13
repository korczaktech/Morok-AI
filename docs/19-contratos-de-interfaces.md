# 19. Contratos de interfaces entre camadas

**Estado:** especificação de arquitetura do sistema; contratos conceituais aprovados para orientar a implementação.  
**Norma de implementação:** C++20, tipos explícitos, erros tipados e interfaces versionadas.

## 19.1 Princípios

- O modelo neural não conhece banco de dados, interface gráfica, rede ou ferramentas.
- O sistema cognitivo não acessa diretamente matrizes internas do modelo.
- Toda comunicação entre subsistemas ocorre por contratos públicos.
- Tipos de dados de entrada e saída são explícitos; não usar strings livres como protocolo interno universal.
- Erros são retornados como códigos e contexto diagnóstico, sem depender de exceções para fluxo normal.
- Sessões e estados transitórios pertencem ao chamador e são isolados.
- Interfaces recebem limites de tamanho e cancelamento quando executam trabalho potencialmente longo.

## 19.2 Tipos básicos compartilhados

- `ByteBuffer`: sequência contígua de bytes, sem assumir texto válido.
- `Utf8Text`: texto que passou pela validação UTF-8 estrita.
- `SessionId`: identificador opaco de sessão, gerado com fonte aleatória do sistema.
- `MemoryId`: identificador opaco de registro persistido.
- `ModelVersion`: identificador de arquitetura e parâmetros.
- `RequestId`: identificador de operação para logs e diagnóstico.
- `Timestamp`: instante UTC em formato interno consistente.
- `Status`: sucesso ou código de erro tipado.
- `Result<T>`: valor T ou erro tipado, sem estado parcialmente válido.

Nenhuma interface deve confundir bytes arbitrários com UTF-8 validado.

## 19.3 Contrato IA-LANG ↔ modelo

O adaptador de linguagem é responsável por:
1. validar entrada textual;
2. converter texto em bytes;
3. criar e manter a sessão de inferência;
4. solicitar previsão/geração ao modelo;
5. acumular bytes gerados;
6. validar a saída UTF-8;
7. devolver texto válido ou resultado binário explicitamente solicitado.

O modelo recebe bytes e estado de sessão. Não recebe objetos de conversa, arquivos, ferramentas ou conexões de rede.

Operações mínimas:
- criar sessão;
- resetar sessão;
- processar byte observado;
- prever próximo byte;
- gerar até limite;
- consultar versão e inventário;
- encerrar sessão.

A interface deve permitir substituir a G1 por uma geração futura sem alterar IA-COMM ou IA-CORE.

## 19.4 Contrato IA-LEARN ↔ modelo

IA-LEARN recebe:
- configuração imutável de experimento;
- corpus por fluxo;
- modelo inicializado;
- política de checkpoint;
- cancelamento e limites de recursos.

IA-LEARN devolve:
- artefato de parâmetros versionado;
- métricas;
- relatório de execução;
- diagnóstico de falhas.

Treinamento ocorre fora do caminho normal de inferência. Um modelo em serviço é somente leitura. A promoção de novos pesos exige validação do artefato e troca controlada de versão.

## 19.5 Contrato IA-MEM

Operações mínimas:
- armazenar registro;
- buscar por consulta textual e filtros;
- recuperar por identificador;
- atualizar metadados;
- excluir registro;
- aplicar política de retenção;
- exportar e importar backup.

A interface retorna origem e metadados junto do conteúdo recuperado. Não apresenta memória recuperada como fato confirmado sem preservar sua proveniência.

## 19.6 Contrato IA-REAS

IA-REAS recebe uma tarefa estruturada, contexto autorizado e orçamento de execução. Retorna um plano composto por etapas tipadas, dependências, condições de conclusão e resultados.

O executor valida cada etapa antes de executá-la. IA-REAS não executa comandos arbitrários nem ignora permissões de IA-TOOLS.

## 19.7 Contrato IA-TOOLS

Cada ferramenta declara:
- nome e versão;
- descrição;
- esquema de entrada;
- esquema de saída;
- permissões necessárias;
- efeitos colaterais;
- timeout;
- limite de tamanho;
- política de confirmação.

A chamada recebe identidade da sessão, permissões concedidas e identificador de operação. A ferramenta retorna resultado estruturado e diagnóstico. Não pode ampliar as próprias permissões.

## 19.8 Contrato IA-COMM

IA-COMM recebe mensagens com papel, conteúdo, sessão e metadados limitados. Orquestra IA-CORE, IA-LANG, IA-MEM, IA-REAS e IA-TOOLS por interfaces. Não contém treinamento nem implementa operações matemáticas do modelo.

## 19.9 Erros padronizados

Códigos mínimos:
- `INVALID_ARGUMENT`;
- `INVALID_UTF8`;
- `INPUT_TOO_LARGE`;
- `RESOURCE_LIMIT`;
- `OUT_OF_MEMORY`;
- `MODEL_NOT_LOADED`;
- `MODEL_VERSION_MISMATCH`;
- `CHECKPOINT_CORRUPT`;
- `PERMISSION_DENIED`;
- `CONFIRMATION_REQUIRED`;
- `TOOL_TIMEOUT`;
- `TOOL_FAILURE`;
- `CANCELLED`;
- `INTERNAL_ERROR`.

Erros devem incluir RequestId, subsistema, etapa e mensagem técnica segura. Não incluir segredos nem conteúdo sensível por padrão.

## 19.10 Compatibilidade e versionamento

Cada contrato público recebe versão semântica. Mudanças incompatíveis incrementam versão principal e exigem adaptador ou migração. Checkpoints têm versão independente da API. Não inferir compatibilidade apenas porque dimensões coincidem.
