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

A sessão fica no `localStorage` para permanecer ativa entre visitas, é revalidada no servidor ao abrir o site e expira após sete dias. O logout revoga o identificador da sessão no MongoDB. A recuperação de senha por e-mail ainda depende da configuração de um provedor de envio de mensagens.
