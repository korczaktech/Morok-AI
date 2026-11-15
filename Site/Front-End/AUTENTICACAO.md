# Autenticação do Morok

O site usa a API de contas hospedada no Render e o MongoDB Atlas.

- Serviço: `https://morok-accounts-api.onrender.com`
- Database: `Contas`
- Coleção: `morok_accounts`
- Cadastro: `POST /auth/signup`
- Login: `POST /auth/login`
- Sessão: `GET /auth/me`
- Logout: `POST /auth/logout`

As senhas são armazenadas com scrypt e salt individual; nunca são gravadas em texto puro. O backend usa um usuário MongoDB restrito à database `Contas`. A URI e o segredo JWT ficam somente nas variáveis privadas do Render.

A sessão do navegador fica em `sessionStorage` e expira após sete dias. A recuperação de senha por e-mail ainda depende da configuração de um provedor de envio de mensagens.
