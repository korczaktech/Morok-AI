import { readFileSync } from "node:fs";

const read = (path) => readFileSync(new URL(path, import.meta.url), "utf8");
const html = read("./index.html");
const app = read("./app.js");
const styles = read("./styles.css");
const worker = read("./sw.js");
const wasmApi = read("./wasm/wasm_api.cpp");
const deployWorkflow = read("../../.github/workflows/Pagina.yml");
const backend = read("../Back-End/server.js");
const packageJson = JSON.parse(read("../Back-End/package.json"));

const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]);
const duplicates = ids.filter((id, index) => ids.indexOf(id) !== index);
if (duplicates.length) throw new Error("IDs HTML duplicados: " + [...new Set(duplicates)].join(", "));

const htmlIds = new Set(ids);
const dynamicIds = new Set([
  "create-project-sidebar", "security-sessions", "support-ticket-list", "account-name-input",
  "toggle-2fa", "account-sessions", "support-subject", "support-description",
  "project-name-input", "task-input", "current-password", "new-password",
  "project-rename-input", "notifications-toggle", "location-input", "about-input",
  "style-select", "workspace-theme", "instructions-input"
]);
const selectors = [...app.matchAll(/\$\(['"]#([A-Za-z][\w-]*)['"]\)/g)].map((match) => match[1]);
const missing = [...new Set(selectors.filter((id) => !htmlIds.has(id) && !dynamicIds.has(id)))];
if (missing.length) throw new Error("Seletores sem elemento HTML ou renderização dinâmica: " + missing.join(", "));

const requiredContracts = [
  [app, "/v1/conversations/delete", "frontend deve propagar exclusão de conversas"],
  [backend, 'path === "/v1/conversations/delete"', "backend deve aceitar exclusão de conversas"],
  [backend, "conversationDeletions.deleteMany", "exclusão de conta deve limpar tombstones"],
  [app, "state.tasks = merged.slice(0, 100);", "sincronização deve preservar IDs de tarefas"],
  [app, "pendingResourceDeletions", "falhas de exclusão remota devem ficar pendentes"],
  [app, "pendingConversationDeletions", "exclusões de conversa devem sobreviver a falhas de rede"],
  [app, "Preferências salvas e sincronizadas.", "tema deve sincronizar com a conta"],
  [app, "async function clearLocalPushSubscription()", "push local deve ser removido ao sair da conta"],
  [backend, "createToken(account, req)", "sessão deve registrar user-agent"],
  [backend, "/v1/account/2fa/enable", "backend deve oferecer ativação de 2FA"],
  [backend, "/v1/support/tickets", "backend deve oferecer chamados"],
  [backend, 'path === "/v1/support/tickets/close"', "backend deve permitir encerrar chamados"],
  [app, "data-close-ticket", "frontend deve expor encerramento de chamados"],
  [worker, 'addEventListener("push"', "service worker deve tratar push"],
  [html, "wasm/tsukuyomi.js", "frontend deve carregar o módulo WebAssembly"],
  [app, "getTsukuyomiRuntime", "frontend deve inicializar o motor G2 local"],
  [app, "runtime.hasCheckpoint(prompt)", "frontend deve selecionar checkpoint por pergunta"],
  [app, "raw.githubusercontent.com/korczaktech/Morok-AI/tsukuyomi-state", "frontend deve baixar somente o checkpoint identificado pelo hash"],
  [deployWorkflow, "-sEXPORTED_RUNTIME_METHODS=FS", "build WASM deve expor o filesystem para carregar o checkpoint"],
  [app, "runtime.generate(prompt, 256)", "frontend deve executar inferência no navegador"],
  [app, "/v1/conversations/sync", "respostas devem continuar sincronizadas com o backend"],
  [wasmApi, "question_checkpoint_path", "WASM deve usar o hash de checkpoint compatível com o treinador"],
  [wasmApi, "checkpointFilename", "WASM deve expor o nome estável do checkpoint para download"],
  [wasmApi, "load_checkpoint", "WASM deve validar e carregar os pesos do G2"]
];
for (const [source, expected, description] of requiredContracts) {
  if (!source.includes(expected)) throw new Error("Contrato ausente: " + description);
}

if (!packageJson.dependencies?.mongodb || !packageJson.dependencies?.bcryptjs) {
  throw new Error("Dependências essenciais do backend ausentes.");
}
if (!/^\^10\.6\.0$/.test(packageJson.dependencies?.nodemailer || "")) {
  throw new Error("Nodemailer precisa permanecer na linha corrigida 10.6.0.");
}
if (!/@media/.test(styles)) throw new Error("CSS sem regras responsivas.");
if (!/app\.js\?v=/.test(html) || !/styles\.css\?v=/.test(html)) {
  throw new Error("Frontend sem versionamento de cache dos assets.");
}

process.stdout.write("Validação estrutural do Morok concluída.\n");
