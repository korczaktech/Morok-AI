# 22. Segurança, privacidade e permissões

## 22.1 Princípio

A IA deve operar com privilégio mínimo. Texto gerado, memória recuperada e conteúdo de ferramentas são dados não confiáveis, nunca instruções privilegiadas por si só.

## 22.2 Fronteiras de confiança

- Entrada do usuário: conteúdo não confiável.
- Documentos e páginas externas: conteúdo não confiável.
- Memória recuperada: dados com proveniência, não comandos.
- Saída do modelo: conteúdo não confiável.
- Ferramentas: capacidades isoladas e explicitamente autorizadas.
- Configuração do sistema: fonte confiável somente após validação e controle de acesso.

## 22.3 Permissões

Cada ferramenta declara permissões específicas: leitura, escrita, rede, execução, acesso a arquivos ou outras capacidades. Conceder somente o mínimo necessário. Permissão não pode ser ampliada pelo modelo nem pela ferramenta durante execução.

## 22.4 Confirmação humana

Exigir confirmação antes de:
- excluir ou sobrescrever dados;
- enviar mensagens ou publicar conteúdo externamente;
- realizar compras, pagamentos ou transações;
- executar código ou comandos;
- alterar configurações de segurança;
- compartilhar dados pessoais;
- executar ações irreversíveis.

A confirmação deve apresentar ação, destino e efeito esperado de forma clara. Uma confirmação não pode ser reutilizada para ação diferente.

## 22.5 Execução isolada

Ferramentas executam em processo ou ambiente isolado quando possível, com:
- timeout;
- limite de memória;
- limite de CPU;
- diretório permitido;
- rede desativada por padrão;
- limite de tamanho de entrada/saída;
- cancelamento;
- captura de código de saída.

Não fornecer shell irrestrito na primeira versão.

## 22.6 Proteção de dados

- Não registrar conteúdo sensível por padrão.
- Restringir permissões dos arquivos locais.
- Separar dados por usuário e sessão.
- Não compartilhar estados recorrentes entre sessões.
- Não incluir segredos em logs, prompts de diagnóstico ou relatórios.
- Oferecer exclusão de memórias persistentes e exportação dos dados.
- Criptografia em repouso poderá ser adicionada se o modelo de ameaça exigir; não alegar proteção criptográfica enquanto não estiver implementada e testada.

## 22.7 Resistência a prompt injection

Instruções contidas em documentos, páginas, resultados de busca ou memória não podem alterar políticas, permissões ou configuração. O conteúdo deve ser marcado por origem e tratado como dado. A decisão de chamar ferramenta pertence ao orquestrador com verificação de política.

## 22.8 Falhas e auditoria

Negar por padrão quando permissões, identidade, origem ou confirmação forem ambíguas. Registrar metadados de ações e resultados, sem gravar conteúdo privado desnecessário. Falhas de segurança não podem ser convertidas em sucesso silencioso.
