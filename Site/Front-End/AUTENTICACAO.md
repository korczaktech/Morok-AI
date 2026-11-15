# Autenticação do Morok Web

O frontend usa Supabase Auth para autenticação real por e-mail e senha. O código do navegador chama diretamente os endpoints oficiais de autenticação do projeto Supabase.

## Configuração

1. Crie um projeto no Supabase.
2. Em **Project Settings → API**, copie a **Project URL** e a chave **anon/publicable**.
3. No painel do Supabase, abra **Authentication → URL Configuration** e adicione a URL pública do Morok Web às URLs permitidas.
4. Em **Authentication → Providers**, habilite Email. Defina se a confirmação de e-mail será obrigatória.
5. Abra o Morok Web, entre em **Configurações → Conta e autenticação** e informe Project URL e anon/publicable key.
6. Salve e use **Entrar ou criar conta**.

A chave publicável/anon foi projetada para uso no frontend. Nunca informe a chave `service_role`, uma chave secreta ou credenciais administrativas no site.

## O que funciona

- Cadastro de conta com e-mail e senha.
- Login usando Supabase Auth.
- Fluxo de confirmação por e-mail, quando habilitado no projeto.
- Solicitação de recuperação de senha.
- Encerramento de sessão.
- Sessão mantida na aba/sessão atual do navegador.

## Limitações atuais

- O histórico de conversas continua salvo localmente no navegador. O login não sincroniza conversas entre dispositivos.
- A sessão não é compartilhada com o servidor C++ de inferência. Não exponha o servidor local diretamente à internet.
- Para sincronização de histórico, será necessário criar tabelas no Supabase com políticas RLS por usuário e integrar o frontend a elas.
- A troca de geração/modelo ainda é apenas uma seleção de interface: o servidor atual carrega um checkpoint G1 único e não recebe um identificador de perfil.
- A G3 permanece indisponível porque ainda é apenas documental no repositório.

## Segurança

O frontend armazena apenas a configuração pública do projeto no armazenamento local e mantém o token de sessão em `sessionStorage`. Não armazena a senha. A segurança das contas, confirmação de e-mail, recuperação e política de senha é administrada pelo Supabase.
