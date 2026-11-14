# Etapa 18 — Segurança e integração

**Sequência:** roteiro oficial de 20 etapas do projeto. Esta etapa é distinta da etapa operacional IA-LEARN, também numerada como 18 na sequência interna da G1.

## Objetivo

Aplicar controles de segurança nas fronteiras entre entrada, modelo, memória, sessões, ferramentas, checkpoints e aplicações. A etapa complementa a camada inicial descrita em `docs/46-etapa-13-seguranca.md`; não substitui auditoria externa nem certificação.

## Controles e integração

- **Entradas:** o servidor aceita somente os métodos/rotas previstos, limita cabeçalhos a 16 KiB e corpo a 64 KiB, exige `Content-Length` e JSON, rejeita `Transfer-Encoding`, propriedades JSON duplicadas/desconhecidas, números fora do limite e prompts que não sejam UTF-8 válido.
- **Saídas:** a resposta gerada é validada novamente como UTF-8 e limitada antes da serialização JSON. O endpoint limita geração a 256 bytes.
- **Sessões:** cada conversa usa SessionId e RequestId próprios. A identidade local do servidor é vinculada ao SessionId real da requisição. Ao encerrar a conversa, o escopo de segurança revoga todos os grants e invalida confirmações pendentes daquela sessão, inclusive em caminhos de exceção.
- **Permissões:** SecuritySubsystem mantém negação por padrão, grants exatos por sujeito/sessão/ferramenta/capacidade e confirmação humana de uso único para ações sensíveis. O servidor local não concede capacidades automaticamente; conteúdo gerado não controla grants nem aprovações.
- **Checkpoints:** MOROKCKP valida magic, versão, arquitetura, inventário de 1.000 parâmetros, comprimentos, CRC32, valores finitos e tamanho máximo de 16 MiB. Escrita é temporária, atômica e sincronizada; o arquivo temporário é criado com permissões privadas do sistema (0600). Teste verifica permissões e rejeição de corrupção.
- **Logs:** eventos de segurança registram somente metadados operacionais. O servidor não registra IDs de principal, prompts, respostas, argumentos, destinos ou segredos nos logs de segurança.
- **Recursos:** o servidor é sequencial, limita sessões e requisições ativas, aplica limites de entrada/geração e timeouts de socket. Em Linux, aplica teto não ampliável de 3 GiB ao espaço de endereçamento virtual do processo, preservando limites herdados mais restritivos. Runtime e IA-TOOLS mantêm seus próprios limites de memória, concorrência, tamanho e duração.
- **Dados externos:** entrada do usuário, documentos, memória recuperada, saída do modelo e saída de ferramentas permanecem classificados como não confiáveis. Nenhuma dessas origens pode fornecer configuração confiável ou permissões.

## Testes

- `security.security_subsystem`: grants isolados por sessão, confirmações exatas e de uso único, limpeza de grants e confirmações no encerramento da sessão, limites de configuração e falha de auditoria.
- `apps.application_runtime`: inicializa os subsistemas pelo IA-CORE, cria conversa e contexto de requisição, verifica grant vinculado à sessão e confirma que o encerramento limpa o estado de segurança.
- `model.g1_checkpoint`: round-trip, rejeição de CRC corrompido e permissões privadas do arquivo.
- A suíte existente inclui testes de parser/UTF-8, checkpoint, integração, fuzzing e sanitizers.

## Limitações explícitas

- O servidor continua restrito a `127.0.0.1`; não implementa autenticação remota, TLS ou autorização de rede. Não deve ser exposto à Internet.
- `RLIMIT_AS` limita espaço virtual, não mede RSS exato. O teto de 3 GiB é uma barreira do processo, não uma garantia de consumo físico.
- Não há sandbox de processo para handlers de ferramentas, isolamento por namespace/seccomp/job objects nem bloqueio de rede no SO. Handlers registrados continuam sendo código confiável no processo principal.
- Não há criptografia autenticada de checkpoints em repouso nem gestão de chaves. CRC32 detecta corrupção acidental, mas não autentica arquivos contra adulteração intencional.
- A classificação de origem não detecta semanticamente todos os ataques de prompt injection; a aplicação deve continuar separando instruções confiáveis de dados não confiáveis.
- A integração não equivale a auditoria de segurança independente.

## Critério de conclusão

A etapa é considerada implementada quando as fronteiras de entrada/saída, sessão, permissão, checkpoint, log e recurso estão ligadas por contratos verificáveis e testes automatizados. A validação do commit exige CI GCC/Clang Debug/Release, ASan/UBSan e fuzzing aprovados. As limitações acima permanecem declaradas e não podem ser apresentadas como controles implementados.
