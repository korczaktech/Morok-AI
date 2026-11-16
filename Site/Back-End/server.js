import http from "node:http";
import crypto from "node:crypto";
import { MongoClient } from "mongodb";
import bcrypt from "bcryptjs";
import nodemailer from "nodemailer";
import webpush from "web-push";

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
const accountResources = appDb.collection("account_resources");
const supportTickets = appDb.collection("support_tickets");
const pushSubscriptions = appDb.collection("push_subscriptions");
const recoveryTokens = appDb.collection("password_recovery_tokens");

await conversations.createIndex({ accountId: 1, updatedAt: -1 }, { name: "morok_conversations_account_updated" });
await conversations.createIndex({ accountId: 1, conversationId: 1 }, { name: "morok_conversations_account_id_unique" });
await messages.createIndex({ accountId: 1, conversationId: 1, createdAt: 1 }, { name: "morok_messages_conversation_time" });
await messages.createIndex({ accountId: 1, conversationId: 1, messageId: 1 }, { name: "morok_messages_id_unique" });
await accountResources.createIndex({ accountId: 1, type: 1, resourceId: 1 }, { unique: true, name: "morok_account_resource_unique" });
await supportTickets.createIndex({ accountId: 1, createdAt: -1 }, { name: "morok_support_tickets_account_date" });
await pushSubscriptions.createIndex({ accountId: 1, endpoint: 1 }, { unique: true, name: "morok_push_subscription_unique" });
await recoveryTokens.createIndex({ tokenHash: 1 }, { unique: true, name: "morok_recovery_token_unique" });
await recoveryTokens.createIndex({ expiresAt: 1 }, { expireAfterSeconds: 0, name: "morok_recovery_expiry_ttl" });
await sessions.createIndex({ jti: 1 }, { unique: true, name: "morok_sessions_jti_unique" });
await sessions.createIndex({ expiresAt: 1 }, { expireAfterSeconds: 0, name: "morok_sessions_expiry_ttl" });


const pushReady = Boolean(process.env.VAPID_PUBLIC_KEY && process.env.VAPID_PRIVATE_KEY);
if (pushReady) webpush.setVapidDetails(process.env.VAPID_SUBJECT || "mailto:admin@korczaktech.com.br", process.env.VAPID_PUBLIC_KEY, process.env.VAPID_PRIVATE_KEY);

const mailer = process.env.SMTP_HOST && process.env.SMTP_USER && process.env.SMTP_PASS
  ? nodemailer.createTransport({
      host: process.env.SMTP_HOST,
      port: Number(process.env.SMTP_PORT || 587),
      secure: process.env.SMTP_SECURE === "true",
      auth: { user: process.env.SMTP_USER, pass: process.env.SMTP_PASS }
    })
  : null;

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


function base32Encode(buffer) {
  const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  let bits = 0, value = 0, output = "";
  for (const byte of buffer) { value = (value << 8) | byte; bits += 8; while (bits >= 5) { output += alphabet[(value >>> (bits - 5)) & 31]; bits -= 5; } }
  if (bits) output += alphabet[(value << (5 - bits)) & 31];
  return output;
}
function base32Decode(input) {
  const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  let bits = 0, value = 0; const bytes = [];
  for (const char of String(input).replace(/=+$/,"").toUpperCase()) { const n = alphabet.indexOf(char); if (n < 0) throw new Error("Segredo TOTP inválido."); value = (value << 5) | n; bits += 5; if (bits >= 8) { bytes.push((value >>> (bits - 8)) & 255); bits -= 8; } }
  return Buffer.from(bytes);
}
function totp(secret, time = Date.now()) {
  const counter = Math.floor(time / 30000);
  const message = Buffer.alloc(8); message.writeBigUInt64BE(BigInt(counter));
  const digest = crypto.createHmac("sha1", base32Decode(secret)).update(message).digest();
  const offset = digest[digest.length - 1] & 15;
  const binary = (digest.readUInt32BE(offset) & 0x7fffffff) % 1000000;
  return String(binary).padStart(6, "0");
}
function verifyTotp(secret, code) {
  if (!/^\d{6}$/.test(String(code || ""))) return false;
  const now = Date.now();
  return [-1,0,1].some(step => totp(secret, now + step * 30000) === String(code));
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

async function createToken(account, req) {
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
  await sessions.insertOne({ jti, accountId: account.id, userAgent: String(req?.headers?.['user-agent'] || '').slice(0, 250), createdAt: new Date(), expiresAt });
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

  if (req.method === "POST" && ["/auth/signup", "/auth/login", "/auth/recover", "/auth/reset"].includes(path)) {
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
        access_token: await createToken(account, req),
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
      if (account.Seguranca?.TwoFactorEnabled && !verifyTotp(account.Seguranca.TwoFactorSecret, body.totp)) return send(res, 401, { error: "Código de autenticação em dois fatores necessário ou inválido.", two_factor_required: true }, req.allowedOrigin);
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
    if (!mailer) return send(res, 503, { error: "Recuperação por e-mail indisponível: configure SMTP_HOST, SMTP_USER, SMTP_PASS e SMTP_FROM no serviço." }, req.allowedOrigin);
    try {
      const body = await readBody(req);
      const email = normalizeEmail(body.email);
      if (!validEmail(email)) return send(res, 400, { error: "Informe um e-mail válido." }, req.allowedOrigin);
      const account = await accounts.findOne({ Email: email }, { projection: { id: 1, Email: 1 } });
      if (account) {
        const rawToken = crypto.randomBytes(32).toString("base64url");
        const tokenHash = crypto.createHash("sha256").update(rawToken).digest("hex");
        await recoveryTokens.deleteMany({ accountId: account.id });
        await recoveryTokens.insertOne({ accountId: account.id, tokenHash, createdAt: new Date(), expiresAt: new Date(Date.now() + 30 * 60 * 1000) });
        const baseUrl = process.env.PUBLIC_SITE_URL || "https://morokai.korczaktech.com.br";
        const resetUrl = baseUrl + "/?reset=" + encodeURIComponent(rawToken);
        await mailer.sendMail({ from: process.env.SMTP_FROM || process.env.SMTP_USER, to: account.Email, subject: "Redefinição de senha do Morok", text: "Para redefinir sua senha, abra este link em até 30 minutos:\n\n" + resetUrl + "\n\nSe você não solicitou, ignore esta mensagem." });
      }
      return send(res, 200, { success: true, message: "Se o endereço estiver cadastrado, enviaremos instruções de recuperação." }, req.allowedOrigin);
    } catch {
      return send(res, 503, { error: "Não foi possível enviar o e-mail de recuperação." }, req.allowedOrigin);
    }
  }

  if (req.method === "POST" && path === "/auth/reset") {
    const body = await readBody(req);
    const rawToken = typeof body.token === "string" ? body.token : "";
    const password = typeof body.password === "string" ? body.password : "";
    if (password.length < 10 || password.length > 128) return send(res, 400, { error: "A senha deve ter entre 10 e 128 caracteres." }, req.allowedOrigin);
    const tokenHash = crypto.createHash("sha256").update(rawToken).digest("hex");
    const recovery = await recoveryTokens.findOne({ tokenHash, expiresAt: { $gt: new Date() } });
    if (!recovery) return send(res, 400, { error: "Link inválido ou expirado." }, req.allowedOrigin);
    await accounts.updateOne({ id: recovery.accountId }, { $set: { "Autenticacao.SenhaHash": await bcrypt.hash(password, 12), PasswordChangedAt: new Date(), "Conta.AtualizadaEm": new Date() } });
    await sessions.deleteMany({ accountId: recovery.accountId });
    await recoveryTokens.deleteMany({ accountId: recovery.accountId });
    return send(res, 200, { success: true }, req.allowedOrigin);
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
        updatedAt: item.updatedAt, projectId: item.projectId || null, messages: grouped.get(item.conversationId) || []
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
        { $set: { title: String(item.title || "Nova conversa").slice(0, 160), projectId: typeof item.projectId === 'string' ? item.projectId.slice(0, 128) : null, updatedAt },
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
      const newAssistantMessages = [];
      for (const message of item.messages) {
        if (message?.role === "assistant" && typeof message.id === "string") {
          const exists = await messages.findOne({ accountId: token.sub, conversationId: item.id, messageId: message.id }, { projection: { _id: 1 } });
          if (!exists) newAssistantMessages.push(message);
        }
      }
      if (operations.length) await messages.bulkWrite(operations, { ordered: false });
      if (pushReady && newAssistantMessages.length) {
        const subs = await pushSubscriptions.find({ accountId: token.sub }).toArray();
        const newest = newAssistantMessages[newAssistantMessages.length - 1];
        const payload = JSON.stringify({ title: "Morok", body: String(newest.content || "Sua resposta está pronta.").slice(0, 180), url: "/" });
        await Promise.allSettled(subs.map(async entry => {
          try { await webpush.sendNotification(entry.subscription, payload); }
          catch (error) { if (error.statusCode === 404 || error.statusCode === 410) await pushSubscriptions.deleteOne({ _id: entry._id }); }
        }));
      }
      return send(res, 200, { success: true, savedMessages: operations.length }, req.allowedOrigin);
    } catch (error) {
      return send(res, error.status || 500, { error: error.status ? error.message : "Não foi possível salvar a conversa." }, req.allowedOrigin);
    }
  }


  if (req.method === "GET" && path === "/v1/account/profile") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const account = await accounts.findOne({ id: token.sub }, { projection: { id: 1, Email: 1, Nome: 1, Preferencias: 1, Seguranca: 1 } });
    if (!account) return send(res, 404, { error: "Conta não encontrada." }, req.allowedOrigin);
    return send(res, 200, { profile: { id: account.id, name: account.Nome || "", email: account.Email, preferences: account.Preferencias || {}, twoFactorEnabled: account.Seguranca?.TwoFactorEnabled === true } }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/account/profile") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const body = await readBody(req);
    const name = typeof body.name === "string" ? body.name.trim().slice(0, 64) : "";
    const preferences = body.preferences && typeof body.preferences === "object" && !Array.isArray(body.preferences) ? body.preferences : {};
    if (!name) return send(res, 400, { error: "O nome não pode ficar vazio." }, req.allowedOrigin);
    const safePreferences = {
      Idioma: typeof preferences.language === "string" ? preferences.language.slice(0, 12) : "pt-BR",
      Tema: ["dark", "light", "system"].includes(preferences.theme) ? preferences.theme : "system"
    };
    await accounts.updateOne({ id: token.sub }, { $set: { Nome: name, Preferencias: safePreferences, "Conta.AtualizadaEm": new Date() } });
    return send(res, 200, { success: true, profile: { name, preferences: safePreferences } }, req.allowedOrigin);
  }

  if (req.method === "GET" && path === "/v1/account/sessions") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const docs = await sessions.find({ accountId: token.sub, expiresAt: { $gt: new Date() } }, { projection: { jti: 1, userAgent: 1, createdAt: 1, expiresAt: 1 } }).sort({ createdAt: -1 }).limit(50).toArray();
    return send(res, 200, { sessions: docs.map(x => ({ id: x.jti, createdAt: x.createdAt, expiresAt: x.expiresAt, userAgent: x.userAgent || 'Dispositivo não identificado', current: x.jti === token.jti })) }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/account/sessions/revoke") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const body = await readBody(req);
    if (body.all === true) {
      await sessions.deleteMany({ accountId: token.sub, jti: { $ne: token.jti } });
      return send(res, 200, { success: true, revoked: "others" }, req.allowedOrigin);
    }
    if (typeof body.sessionId !== "string" || body.sessionId === token.jti) return send(res, 400, { error: "Selecione outra sessão válida." }, req.allowedOrigin);
    const result = await sessions.deleteOne({ accountId: token.sub, jti: body.sessionId });
    return send(res, 200, { success: true, revoked: result.deletedCount === 1 }, req.allowedOrigin);
  }


  if (req.method === "POST" && path === "/v1/account/2fa/setup") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const secret = base32Encode(crypto.randomBytes(20));
    await accounts.updateOne({ id: token.sub }, { $set: { "Seguranca.TwoFactorSecretPending": secret } });
    const account = await accounts.findOne({ id: token.sub }, { projection: { Email: 1 } });
    const label = encodeURIComponent("Morok:" + (account?.Email || token.sub));
    return send(res, 200, { secret, otpauth: "otpauth://totp/" + label + "?secret=" + secret + "&issuer=Morok&algorithm=SHA1&digits=6&period=30" }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/account/2fa/enable") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const body = await readBody(req);
    const account = await accounts.findOne({ id: token.sub });
    const secret = account?.Seguranca?.TwoFactorSecretPending;
    if (!secret || !verifyTotp(secret, body.code)) return send(res, 400, { error: "Código inválido. Verifique o aplicativo autenticador." }, req.allowedOrigin);
    await accounts.updateOne({ id: token.sub }, { $set: { "Seguranca.TwoFactorEnabled": true, "Seguranca.TwoFactorSecret": secret }, $unset: { "Seguranca.TwoFactorSecretPending": "" } });
    return send(res, 200, { success: true, enabled: true }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/account/2fa/disable") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const body = await readBody(req);
    const account = await accounts.findOne({ id: token.sub });
    if (!account || !(await bcrypt.compare(String(body.password || ""), account.Autenticacao?.SenhaHash || "")) || !verifyTotp(account.Seguranca?.TwoFactorSecret, body.code)) return send(res, 401, { error: "Senha ou código de autenticação inválido." }, req.allowedOrigin);
    await accounts.updateOne({ id: token.sub }, { $set: { "Seguranca.TwoFactorEnabled": false }, $unset: { "Seguranca.TwoFactorSecret": "", "Seguranca.TwoFactorSecretPending": "" } });
    return send(res, 200, { success: true, enabled: false }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/account/password") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const body = await readBody(req);
    const currentPassword = typeof body.currentPassword === "string" ? body.currentPassword : "";
    const newPassword = typeof body.newPassword === "string" ? body.newPassword : "";
    const account = await accounts.findOne({ id: token.sub });
    if (!account || !(await bcrypt.compare(currentPassword, account.Autenticacao?.SenhaHash || ""))) return send(res, 401, { error: "Senha atual incorreta." }, req.allowedOrigin);
    if (newPassword.length < 10 || newPassword.length > 128) return send(res, 400, { error: "A nova senha deve ter entre 10 e 128 caracteres." }, req.allowedOrigin);
    await accounts.updateOne({ id: token.sub }, { $set: { "Autenticacao.SenhaHash": await bcrypt.hash(newPassword, 12), PasswordChangedAt: new Date(), "Conta.AtualizadaEm": new Date() } });
    await sessions.deleteMany({ accountId: token.sub, jti: { $ne: token.jti } });
    return send(res, 200, { success: true, otherSessionsRevoked: true }, req.allowedOrigin);
  }

  if (req.method === "GET" && path === "/v1/resources") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const type = url.searchParams.get("type");
    if (!["projects", "tasks"].includes(type)) return send(res, 400, { error: "Tipo de recurso inválido." }, req.allowedOrigin);
    const docs = await accountResources.find({ accountId: token.sub, type }).sort({ updatedAt: -1 }).limit(500).toArray();
    return send(res, 200, { items: docs.map(x => ({ id: x.resourceId, data: x.data, updatedAt: x.updatedAt })) }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/resources/delete") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const body = await readBody(req);
    if (!["projects", "tasks"].includes(body.type) || !Array.isArray(body.ids) || body.ids.length > 500) return send(res, 400, { error: "Solicitação de exclusão inválida." }, req.allowedOrigin);
    const ids = body.ids.filter(value => typeof value === "string" && value.length > 0 && value.length <= 128);
    const result = ids.length ? await accountResources.deleteMany({ accountId: token.sub, type: body.type, resourceId: { $in: ids } }) : { deletedCount: 0 };
    return send(res, 200, { success: true, deleted: result.deletedCount }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/resources/sync") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const body = await readBody(req, 512_000);
    const type = body.type;
    const items = body.items;
    if (!["projects", "tasks"].includes(type) || !Array.isArray(items) || items.length > 500) return send(res, 400, { error: "Lote de sincronização inválido." }, req.allowedOrigin);
    const ops = [];
    for (const item of items) {
      if (!item || typeof item.id !== "string" || !item.id || item.id.length > 128 || !item.data || typeof item.data !== "object") continue;
      const data = JSON.parse(JSON.stringify(item.data));
      if (JSON.stringify(data).length > 20_000) continue;
      ops.push({ updateOne: { filter: { accountId: token.sub, type, resourceId: item.id }, update: { $set: { data, updatedAt: new Date() }, $setOnInsert: { accountId: token.sub, type, resourceId: item.id, createdAt: new Date() } }, upsert: true } });
    }
    if (ops.length) await accountResources.bulkWrite(ops, { ordered: false });
    return send(res, 200, { success: true, saved: ops.length }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/support/tickets") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Entre na conta para abrir um chamado." }, req.allowedOrigin);
    const body = await readBody(req, 32_768);
    const subject = typeof body.subject === "string" ? body.subject.trim().slice(0, 120) : "";
    const description = typeof body.description === "string" ? body.description.trim().slice(0, 8000) : "";
    if (subject.length < 4 || description.length < 10) return send(res, 400, { error: "Informe assunto e descrição suficientes." }, req.allowedOrigin);
    const ticket = { ticketId: crypto.randomUUID(), accountId: token.sub, subject, description, status: "open", createdAt: new Date(), updatedAt: new Date() };
    await supportTickets.insertOne(ticket);
    return send(res, 201, { success: true, ticket: { id: ticket.ticketId, subject, status: ticket.status, createdAt: ticket.createdAt } }, req.allowedOrigin);
  }

  if (req.method === "GET" && path === "/v1/support/tickets") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const docs = await supportTickets.find({ accountId: token.sub }, { projection: { ticketId: 1, subject: 1, status: 1, createdAt: 1, updatedAt: 1 } }).sort({ createdAt: -1 }).limit(100).toArray();
    return send(res, 200, { tickets: docs.map(x => ({ id: x.ticketId, subject: x.subject, status: x.status, createdAt: x.createdAt, updatedAt: x.updatedAt })) }, req.allowedOrigin);
  }

  if (req.method === "GET" && path === "/v1/push/config") {
    return send(res, 200, { enabled: pushReady, publicKey: pushReady ? process.env.VAPID_PUBLIC_KEY : null }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/push/subscribe") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const body = await readBody(req, 16_384);
    const sub = body.subscription;
    if (!sub || typeof sub.endpoint !== "string" || !sub.endpoint.startsWith("https://") || !sub.keys?.p256dh || !sub.keys?.auth) return send(res, 400, { error: "Inscrição push inválida." }, req.allowedOrigin);
    await pushSubscriptions.updateOne({ accountId: token.sub, endpoint: sub.endpoint }, { $set: { subscription: sub, updatedAt: new Date() }, $setOnInsert: { accountId: token.sub, endpoint: sub.endpoint, createdAt: new Date() } }, { upsert: true });
    return send(res, 200, { success: true, pushConfigured: pushReady }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/push/unsubscribe") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const body = await readBody(req, 16_384);
    if (typeof body.endpoint === "string") await pushSubscriptions.deleteOne({ accountId: token.sub, endpoint: body.endpoint });
    return send(res, 200, { success: true }, req.allowedOrigin);
  }

  if (req.method === "POST" && path === "/v1/account/delete") {
    const token = await verifyToken(bearer(req));
    if (!token) return send(res, 401, { error: "Autenticação necessária." }, req.allowedOrigin);
    const body = await readBody(req);
    const account = await accounts.findOne({ id: token.sub });
    if (!account || !(await bcrypt.compare(String(body.password || ""), account.Autenticacao?.SenhaHash || ""))) return send(res, 401, { error: "Confirme sua senha para excluir a conta." }, req.allowedOrigin);
    await Promise.all([
      sessions.deleteMany({ accountId: token.sub }),
      conversations.deleteMany({ accountId: token.sub }),
      messages.deleteMany({ accountId: token.sub }),
      accountResources.deleteMany({ accountId: token.sub }),
      supportTickets.deleteMany({ accountId: token.sub }),
      pushSubscriptions.deleteMany({ accountId: token.sub }),
      recoveryTokens.deleteMany({ accountId: token.sub })
    ]);
    await accounts.deleteOne({ _id: account._id });
    return send(res, 200, { success: true, accountDeleted: true }, req.allowedOrigin);
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
