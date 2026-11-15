# Morok Accounts API

API de autenticação do site Morok. Os registros são armazenados no MongoDB Atlas, database `Contas`, coleção `morok_accounts`.

## Variáveis de ambiente

- `MONGODB_URI`: URI privada do cluster Atlas; nunca versionar.
- `MONGODB_DATABASE`: `Contas`.
- `MONGODB_COLLECTION`: `morok_accounts`.
- `JWT_SECRET`: segredo aleatório com pelo menos 32 caracteres.
- `CORS_ORIGINS`: origens permitidas separadas por vírgula.

## Rotas

- `GET /health`: verifica a conexão com o MongoDB.
- `POST /auth/signup`: cria conta e retorna token.
- `POST /auth/login`: autentica e retorna token.
- `GET /auth/me`: valida o token.
- `POST /auth/logout`: encerra a sessão no cliente (tokens são stateless).
- `POST /auth/recover`: indisponível até configurar um provedor de e-mail.

Senhas são armazenadas com scrypt e salt individual. Tokens JWT HS256 expiram em sete dias.


## Sessões de autenticação

As sessões são registradas na coleção `morok_sessions` dentro da database `Contas`. Cada JWT contém um identificador individual (`jti`), validado contra o registro ativo no MongoDB. O logout remove esse registro e invalida imediatamente o token. Um índice TTL remove sessões expiradas automaticamente após sete dias.
