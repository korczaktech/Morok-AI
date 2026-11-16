import { readFileSync } from "node:fs";

const read = (path) => readFileSync(new URL(path, import.meta.url), "utf8");
const html = read("./index.html");
const app = read("./app.js");
const styles = read("./styles.css");
const worker = read("./sw.js");
const backend = read("../Back-End/server.js");
const packageJson = JSON.parse(read("../Back-End/package.json"));

const ids = [...html.matchAll(/\\bid="([^"]+)"/g)].map((match) => match[1]);
const duplicates = ids.filter((id, index) => ids.indexOf(id) !== index);
if (duplicates.length) throw new Error("IDs HTML duplicados: " + [...new Set(duplicates)].join(", "));

const htmlIds = new Set(ids);
const dynamicIds = new Set(["create-project-sidebar"]);
const selectors = [...app.matchAll(/\\$\\(['"]#([A-Za-z][\\w-]*)['"]\\)/g)].map((match) => match[1]);
const missing = [...new Set(selectors.filter((id) => !htmlIds.has(id) && !dynamicIds.has(id)))];
if (missing.length) throw new Error("Seletores sem elemento HTML ou renderização dinâmica: " + missing.join(", "));

const requiredContracts = [
  [app, "/v1/conversations/delete", "frontend deve propagar exclusão de conversas"],
  [backend, 'path === "/v1/conversations/delete"', "backend deve aceitar exclusão de conversas"],
  [backend, "conversationDeletions.deleteMany", "exclusão de conta deve limpar tombstones"],
  [app, "state.tasks = merged.slice(0, 100);", "sincronização deve preservar IDs de tarefas"],
  [backend, "createToken(account, req)", "sessão deve registrar user-agent"],
  [backend, "/v1/account/2fa/enable", "backend deve oferecer ativação de 2FA"],
  [backend, "/v1/support/tickets", "backend deve oferecer chamados"],
  [worker, 'addEventListener("push"', "service worker deve tratar push"]
];
for (const [source, expected, description] of requiredContracts) {
  if (!source.includes(expected)) throw new Error("Contrato ausente: " + description);
}

if (!packageJson.dependencies?.mongodb || !packageJson.dependencies?.bcryptjs) {
  throw new Error("Dependências essenciais do backend ausentes.");
}
if (!/@media/.test(styles)) throw new Error("CSS sem regras responsivas.");
if (!/app\\.js\\?v=/.test(html) || !/styles\\.css\\?v=/.test(html)) {
  throw new Error("Frontend sem versionamento de cache dos assets.");
}

process.stdout.write("Validação estrutural do Morok concluída.\\n");
