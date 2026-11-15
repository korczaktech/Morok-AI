import http from "node:http";
import crypto from "node:crypto";
import { MongoClient } from "mongodb";
import bcrypt from "bcryptjs";

const PORT = Number(process.env.PORT || 10000);
const MONGODB_URI = process.env.MONGODB_URI;
const DATABASE = process.env.MONGODB_DATABASE || "Contas";
const APP_DATABASE = process.env.MONGODB_APP_DATABASE || "Morok-AI";
const COLLECTION = process.env.MONGODB_COLLECTION || "contas";
const JWT_SECRET = process.env.JWT_SECRET;
const ALLOWED_ORIGINS = new Set([
  ...((process.env.CORS_ORIGINS || "").split(",").map((value) => value.trim()).filter(Boolean)),
  "https://morokai.korczaktech.com.br",
  "https://www.morokai.korczaktech.com.br",
  "https://korczaktech.github.io"
]);

function isAllowedOrigin(origin) {
  if (!origin) return true;
  if (ALLOWED_ORIGINS.has(origin)) return true;
  try {
    const parsed = new URL(origin);
    return parsed.protocol === "https:" &&
      (parsed.hostname === "korczaktech.com.br" ||
       parsed.hostname.endsWith(".korczaktech.com.br"));
  } catch {
    return false;
  }
}

if (!MONGODB_URI || !JWT_SECRET || JWT_SECRET.length < 32) {
  throw new Error("MONGODB_URI e JWT_SECRET (mínimo 32 caracteres) são obrigatórios.");
}

const client = new MongoClient(MONGODB_URI);
await client.connect();
const db = client.db(DATABASE);
const accounts = db.collection(COLLECTION);
const sessions = db.collection("morok_sessions");
const appDb = client.db(APP_DATABASE);
const conversations = appDb.collection("conversations");
const messages = appDb.collection("messages");
await conversations.createIndex({ accountId: 1, updatedAt: -1 }, { name: "morok_conversations_account_updated" });
await conversations.createIndex({ accountId: 1, conversationId: 1 }, { name: "morok_conversations_account_id_unique" });
await messages.createIndex({ accountId: 1, conversationId: 1, createdAt: 1 }, { name: "morok_messages_conversation_time" });
await messages.createIndex({ accountId: 1, conversationId: 1, messageId: 1 }, { name: "morok_messages_id_unique" });
await sessions.createIndex({ jti: 1 }, { unique: true, name: "morok_sessions_jti_unique" });
await sessions.createIndex({ expiresAt: 1 }, { expireAfterSeconds: 0, name: "morok_sessions_expiry_ttl" });

const attempts = new Map();
const WINDOW_MS = 15 * 60 * 1000;
const MAX_ATTEMPTS = 12;
const TOKEN_TTL_SECONDS = 7 * 24 * 60 * 60;

function send(res, status, payload, origin) {
  const headers = {
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": "no-store",
    "X-Content-Type-Options": "nosniff",
    "Referrer-Policy": "no-referrer"
  };
  if (origin && isAllowedOrigin(origin)) {
    headers["Access-Control-Allow-Origin"] = origin;
    headers["Vary"] = "Origin";
    headers["Access-Control-Allow-Credentials"] = "false";
  }
  res.writeHead(status, headers);
  res.end(JSON.stringify(payload));
}

function cors(req, res) {
  const origin = req.headers.origin;
  if (origin && !isAllowedOrigin(origin)) {
    send(res, 403, { error: "Origem não autorizada." });
    return false;
  }
  if (req.method === "OPTIONS") {
    res.writeHead(204, {
      "Access-Control-Allow-Origin": origin || "null",
      "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type, Authorization",
      "Access-Control-Max-Age": "600",
      "Vary": "Origin"
    });
    res.end();
    return false;
  }
  req.allowedOrigin = origin;
  return true;
}

async function readBody(req, maxBytes = 16_384) {
  let raw = "";
  for await (const chunk of req) {
    raw += chunk;
    if (Buffer.byteLength(raw, "utf8") > maxBytes) throw Object.assign(new Error("Corpo da requisição muito grande."), { status: 413 });
  }
  try { return raw ? JSON.parse(raw) : {}; }
  catch { throw Object.assign(new Error("JSON inválido."), { status: 400 }); }
}

function rateLimit(req) {
  const key = (req.socket.remoteAddress || "unknown") + ":" + req.url;
  const now = Date.now();
  const current = attempts.get(key);
  if (!current || now - current.startedAt > WINDOW_MS) {
    attempts.set(key, { startedAt: now, count: 1 });
    return true;
  }
  current.count += 1;
  return current.count <= MAX_ATTEMPTS;
}

function normalizeEmail(value) {
  return typeof value === "string" ? value.trim().toLowerCase() : "";
}

function validEmail(email) {
  return email.length <= 254 && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email);
}

function base64url(value) {
  return Buffer.from(value).toString("base64url");
}

async function createToken(account) {
  const issuedAt = Math.floor(Date.now() / 1000);
  const expiresAt = new Date((issuedAt + TOKEN_TTL_SECONDS) * 1000);
  const jti = crypto.randomUUID();
  const header = base64url(JSON.stringify({ alg: "HS256", typ: "JWT" }));
  const payload = base64url(JSON.stringify({
    sub: account.id,
    email: account.Email,
    jti,
    iat: issuedAt,
    exp: issuedAt + TOKEN_TTL_SECONDS
  }));
  const unsigned = header + "." + payload;
  const signature = crypto.createHmac("sha256", JWT_SECRET).update(unsigned).digest("base64url");
  await sessions.insertOne({ jti, accountId: account.id, createdAt: new Date(), expiresAt });
  return unsigned + "." + signature;
}

async function verifyToken(token) {
  const parts = String(token || "").split(".");
  if (parts.length !== 3) return null;
  const unsigned = parts[0] + "." + parts[1];
  const expected = crypto.createHmac("sha256", JWT_SECRET).update(unsigned).digest();
  let actual;
  try { actual = Buffer.from(parts[2], "base64url"); } catch { return null; }
  if (actual.length !== expected.length || !crypto.timingSafeEqual(actual, expected)) return null;
  try {
    const header = JSON.parse(Buffer.from(parts[0], "base64url").toString("utf8"));
    if (header.alg !== "HS256" || header.typ !== "JWT") return null;
    const payload = JSON.parse(Buffer.from(parts[1], "base64url").toString("utf8"));
    if (!payload.sub || !payload.jti || !payload.exp || payload.exp <= Math.floor(Date.now() / 1000)) return null;
    const active = await sessions.findOne({ jti: payload.jti, accountId: payload.sub, expiresAt: { $gt: new Date() } }, { projection: { _id: 1 } });
    return active ? payload : null;
  } catch { return null; }
}

function publicAccount(account) {
  const email = account.Email;
  return { id: account.id, email, name: account.Nome || email.split("@")[0] };
}

function bearer(req) {
  const value = req.headers.authorization || "";
  return value.startsWith("Bearer ") ? value.slice(7) : "";
}

async function handle(req, res) {
  if (!cors(req, res)) return;
  const url = new URL(req.url, "http://localhost");
  const path = url.pathname;

  if (req.method === "GET" && path === "/health") {
    return send(res, 200, { status: "ok" }, req.allowedOrigin);
  }

  if (req.method === "POST" && ["/auth/signup", "/auth/login", "/auth/recover"].includes(path)) {
    if (!rateLimit(req)) return send(res, 429, { error: "Muitas tentativas. Aguarde 15 minutos." }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/auth/signup") {
    try {
      const body = await readBody(req);
      const email = normalizeEmail(body.email);
      const password = typeof body.password === "string" ? body.password : "";
      const name = typeof body.name === "string" ? body.name.trim().slice(0, 64) : "";
      if (!validEmail(email)) return send(res, 400, { error: "Informe um e-mail válido." }, req.allowedOrigin);
      if (password.length < 8 || password.length > 128) return send(res, 400, { error: "A senha deve ter entre 8 e 128 caracteres." }, req.allowedOrigin);
      const existing = await accounts.findOne({ Email: email }, { projection: { _id: 1 } });
      if (existing) return send(res, 409, { error: "Já existe uma conta com este e-mail." }, req.allowedOrigin);
      const now = new Date();
      const account = {
        id: crypto.randomUUID(),
        Nome: name || email.split("@")[0].slice(0, 64),
        Email: email,
        Telefone: null,
        Planos: {},
        Verified: false,
        EmailVerified: false,
        PhoneVerified: false,
        Conta: { Status: "active", Role: "user", CriadaEm: now, AtualizadaEm: now, UltimoLogin: now },
        Produtos: {},
        Seguranca: { TwoFactorEnabled: false, RecoveryEnabled: false },
        Preferencias: { Idioma: "pt-BR", Tema: "system" },
        Metadados: { OrigemCadastro: "morok-web", VersaoCadastro: "1.0.0", UltimoDispositivo: "", UltimoIP: null },
        PasswordChangedAt: now,
        Autenticacao: { Email: email, SenhaHash: await bcrypt.hash(password, 12), EmailVerificado: false }
      };
      await accounts.insertOne(account);
      return send(res, 201, {
        access_token: await createToken(account),
        token_type: "Bearer",
        expires_in: TOKEN_TTL_SECONDS,
        user: publicAccount(account)
      }, req.allowedOrigin);
    } catch (error) {
      if (error?.code === 11000) return send(res, 409, { error: "Já existe uma conta com este e-mail." }, req.allowedOrigin);
      return send(res, error.status || 500, { error: error.status ? error.message : "Não foi possível criar a conta." }, req.allowedOrigin);
    }
  }

  if (req.method === "POST" && path === "/auth/login") {
    try {
      const body = await readBody(req);
      const email = normalizeEmail(body.email);
      const password = typeof body.password === "string" ? body.password : "";
      if (!validEmail(email) || !password) return send(res, 400, { error: "E-mail ou senha inválidos." }, req.allowedOrigin);
      const account = await accounts.findOne({ Email: email });
      if (!account || account.Conta?.Status !== "active" || !(await bcrypt.compare(password, account.Autenticacao?.SenhaHash || ""))) {
        return send(res, 401, { error: "E-mail ou senha incorretos." }, req.allowedOrigin);
      }
      const loginAt = new Date();
      await accounts.updateOne({ _id: account._id }, { $set: { "Conta.UltimoLogin": loginAt, "Conta.AtualizadaEm": loginAt } });
      return send(res, 200, {
        access_token: await createToken(account),
        token_type: "Bearer",
        expires_in: TOKEN_TTL_SECONDS,
        user: publicAccount(account)
      }, req.allowedOrigin);
    } catch (error) {
      return send(res, error.status || 500, { error: error.status ? error.message : "Não foi possível entrar na conta." }, req.allowedOrigin);
    }
  }

  if (req.method === "GET" && path === "/auth/me") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Sessão inválida ou expirada." }, req.allowedOrigin);
    const account = await accounts.findOne({ id: token.sub }, { projection: { id: 1, Email: 1, Nome: 1 } });
    if (!account) return send(res, 401, { error: "Conta não encontrada." }, req.allowedOrigin);
    return send(res, 200, { user: publicAccount(account) }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/auth/logout") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Sessão inválida ou expirada." }, req.allowedOrigin);
    await sessions.deleteOne({ jti: token.jti });
    return send(res, 200, { success: true }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/auth/recover") {
    return send(res, 501, { error: "A recuperação por e-mail ainda não foi configurada." }, req.allowedOrigin);
  }

  if (req.method === "GET" && path === "/v1/conversations") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const docs = await conversations.find({ accountId: token.sub }).sort({ updatedAt: -1 }).limit(100).toArray();
    const ids = docs.map((item) => item.conversationId);
    const storedMessages = ids.length
      ? await messages.find({ accountId: token.sub, conversationId: { $in: ids } }).sort({ createdAt: 1 }).toArray()
      : [];
    const grouped = new Map();
    for (const item of storedMessages) {
      if (!grouped.has(item.conversationId)) grouped.set(item.conversationId, []);
      grouped.get(item.conversationId).push({
        id: item.messageId, role: item.role, content: item.content,
        createdAt: item.createdAt, attachments: item.attachments || []
      });
    }
    return send(res, 200, {
      conversations: docs.map((item) => ({
        id: item.conversationId, title: item.title, createdAt: item.createdAt,
        updatedAt: item.updatedAt, messages: grouped.get(item.conversationId) || []
      }))
    }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/conversations/sync") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    try {
      const body = await readBody(req, 512_000);
      const item = body.conversation;
      if (!item || typeof item.id !== "string" || !item.id || item.id.length > 128 ||
          !Array.isArray(item.messages) || item.messages.length > 500) {
        return send(res, 400, { error: "Conversa inválida ou acima do limite." }, req.allowedOrigin);
      }
      const toDate = (value) => {
        const date = value ? new Date(value) : new Date();
        return Number.isNaN(date.getTime()) ? new Date() : date;
      };
      const createdAt = toDate(item.createdAt);
      const updatedAt = toDate(item.updatedAt);
      await conversations.updateOne(
        { accountId: token.sub, conversationId: item.id },
        { $set: { title: String(item.title || "Nova conversa").slice(0, 160), updatedAt },
          $setOnInsert: { accountId: token.sub, conversationId: item.id, createdAt } },
        { upsert: true }
      );
      const operations = [];
      for (const message of item.messages) {
        if (!message || typeof message.id !== "string" || !message.id || message.id.length > 128 ||
            !["user", "assistant", "system"].includes(message.role) ||
            typeof message.content !== "string" || message.content.length > 100_000) continue;
        operations.push({
          updateOne: {
            filter: { accountId: token.sub, conversationId: item.id, messageId: message.id },
            update: { $set: {
              accountId: token.sub, conversationId: item.id, messageId: message.id,
              role: message.role, content: message.content, createdAt: toDate(message.createdAt),
              attachments: Array.isArray(message.attachments) ? message.attachments.slice(0, 8) : []
            } },
            upsert: true
          }
        });
      }
      if (operations.length) await messages.bulkWrite(operations, { ordered: false });
      return send(res, 200, { success: true, savedMessages: operations.length }, req.allowedOrigin);
    } catch (error) {
      return send(res, error.status || 500, { error: error.status ? error.message : "Não foi possível salvar a conversa." }, req.allowedOrigin);
    }
  }

  if (req.method === "POST" && path === "/v1/chat") {
    return send(res, 503, { error: "O motor de inferência ainda não está conectado a esta API." }, req.allowedOrigin);
  }

  return send(res, 404, { error: "Rota não encontrada." }, req.allowedOrigin);
}

const server = http.createServer((req, res) => {
  handle(req, res).catch(() => {
    if (!res.headersSent) send(res, 500, { error: "Erro interno do servidor." }, req.allowedOrigin);
    else res.end();
  });
});

server.listen(PORT, "0.0.0.0", () => {
  console.log("Morok Accounts API listening on port " + PORT);
});

async function shutdown() {
  server.close();
  await client.close();
  process.exit(0);
}
process.on("SIGTERM", shutdown);
process.on("SIGINT", shutdown);
