import http from "node:http";
import crypto from "node:crypto";
import { promisify } from "node:util";
import { MongoClient } from "mongodb";

const scrypt = promisify(crypto.scrypt);
const PORT = Number(process.env.PORT || 10000);
const MONGODB_URI = process.env.MONGODB_URI;
const DATABASE = process.env.MONGODB_DATABASE || "Contas";
const COLLECTION = process.env.MONGODB_COLLECTION || "morok_accounts";
const JWT_SECRET = process.env.JWT_SECRET;
const ALLOWED_ORIGINS = new Set(
  (process.env.CORS_ORIGINS || "https://korczaktech.github.io")
    .split(",").map((value) => value.trim()).filter(Boolean)
);

if (!MONGODB_URI || !JWT_SECRET || JWT_SECRET.length < 32) {
  throw new Error("MONGODB_URI e JWT_SECRET (mínimo 32 caracteres) são obrigatórios.");
}

const client = new MongoClient(MONGODB_URI);
await client.connect();
const db = client.db(DATABASE);
const accounts = db.collection(COLLECTION);
const sessions = db.collection("morok_sessions");
await accounts.createIndex({ email: 1 }, { unique: true, name: "morok_accounts_email_unique" });
await accounts.createIndex({ accountId: 1 }, { unique: true, name: "morok_accounts_account_id_unique" });
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
  if (origin && ALLOWED_ORIGINS.has(origin)) {
    headers["Access-Control-Allow-Origin"] = origin;
    headers["Vary"] = "Origin";
    headers["Access-Control-Allow-Credentials"] = "false";
  }
  res.writeHead(status, headers);
  res.end(JSON.stringify(payload));
}

function cors(req, res) {
  const origin = req.headers.origin;
  if (origin && !ALLOWED_ORIGINS.has(origin)) {
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

async function readBody(req) {
  let raw = "";
  for await (const chunk of req) {
    raw += chunk;
    if (raw.length > 16_384) throw Object.assign(new Error("Corpo da requisição muito grande."), { status: 413 });
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

async function hashPassword(password) {
  const salt = crypto.randomBytes(16).toString("hex");
  const derived = await scrypt(password, salt, 64);
  return "scrypt$" + salt + "$" + derived.toString("hex");
}

async function verifyPassword(password, stored) {
  if (typeof stored !== "string") return false;
  const [algorithm, salt, expectedHex] = stored.split("$");
  if (algorithm !== "scrypt" || !salt || !/^[a-f0-9]{128}$/i.test(expectedHex || "")) return false;
  const actual = await scrypt(password, salt, 64);
  const expected = Buffer.from(expectedHex, "hex");
  return expected.length === actual.length && crypto.timingSafeEqual(actual, expected);
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
    sub: account.accountId,
    email: account.email,
    jti,
    iat: issuedAt,
    exp: issuedAt + TOKEN_TTL_SECONDS
  }));
  const unsigned = header + "." + payload;
  const signature = crypto.createHmac("sha256", JWT_SECRET).update(unsigned).digest("base64url");
  await sessions.insertOne({ jti, accountId: account.accountId, createdAt: new Date(), expiresAt });
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
  return { id: account.accountId, email: account.email, name: account.name || account.email.split("@")[0] };
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
    try {
      await db.command({ ping: 1 });
      return send(res, 200, { status: "ok", database: DATABASE }, req.allowedOrigin);
    } catch {
      return send(res, 503, { status: "unavailable" }, req.allowedOrigin);
    }
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
      const existing = await accounts.findOne({ email }, { projection: { _id: 1 } });
      if (existing) return send(res, 409, { error: "Já existe uma conta com este e-mail." }, req.allowedOrigin);
      const account = {
        accountId: crypto.randomUUID(),
        name: name || email.split("@")[0].slice(0, 64),
        email,
        passwordHash: await hashPassword(password),
        createdAt: new Date(),
        updatedAt: new Date(),
        lastLoginAt: new Date()
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
      const account = await accounts.findOne({ email });
      if (!account || !(await verifyPassword(password, account.passwordHash))) {
        return send(res, 401, { error: "E-mail ou senha incorretos." }, req.allowedOrigin);
      }
      await accounts.updateOne({ _id: account._id }, { $set: { lastLoginAt: new Date(), updatedAt: new Date() } });
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
    const account = await accounts.findOne({ accountId: token.sub }, { projection: { accountId: 1, email: 1, name: 1 } });
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
