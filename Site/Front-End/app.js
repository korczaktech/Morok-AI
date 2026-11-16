(() => {
  'use strict';
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const STORAGE_KEY = 'morok.web.state.v2';
  const AUTH_API_URL = 'https://morok-ai.onrender.com';
  const MAX_LOCAL_BYTES = 3_500_000;
  const MAX_LIBRARY_FILE_BYTES = 48 * 1024;
  const INTEGRATION_CONTRACTS = Object.freeze({chat:{status:'connected',method:'POST',path:'/v1/chat',request:['prompt','max_new_bytes'],response:['response','request_id']},imageGeneration:{status:'pending',request:['prompt','model','size'],response:['image_url','request_id']},modelContext:{status:'pending',fields:['aboutMe','responseStyle','instructions','location','libraryFiles']},externalPlugins:{status:'pending',requirements:['authorization','permissions','server-side connector']}});
  window.MorokIntegrationContracts = INTEGRATION_CONTRACTS;
  const state = { conversations: [], activeId: null, theme: 'dark', apiUrl: AUTH_API_URL, authSession: null, authMode: 'login', generation: 'G1', model: 'Tsukuyomi', displayName: 'Visitante', attachments: [], images: [], tasks: [], projects: [], activeProjectId: null, instructions: '', location: '', aboutMe: '', responseStyle: 'equilibrado', notifications: false, libraryItems: [], pendingResourceDeletions: [], pendingConversationDeletions: [], busy: false, controller: null, contextId: null, renameId: null, confirmResolve: null, toastTimer: null };
  const el = {
    app: $('#app'), sidebar: $('#sidebar'), backdrop: $('#backdrop'), history: $('#history-list'),
    welcome: $('#welcome'), conversation: $('#conversation'), input: $('#message-input'), composer: $('#composer'),
    send: $('#send-button'), tray: $('#attachment-tray'), file: $('#file-input'), status: $('#status-label'),
    dot: $('#status-dot'), pill: $('#connection-pill'), settings: $('#settings-dialog'), rename: $('#rename-dialog'),
    profile: $('#profile-dialog'), auth: $('#auth-dialog'), context: $('#context-menu'), toast: $('#toast'),
    search: $('#search-dialog'), confirm: $('#confirm-dialog')
  };

  const iconPaths = {"brand":"<path d=\"M4 18V6l5 8 3-5 3 5 5-8v12\"/><path d=\"M4 6h4M16 6h4\"/>","close":"<path d=\"m6 6 12 12M18 6 6 18\"/>","new-chat":"<path d=\"M12 4v16M4 12h16\"/><path d=\"M5 5h3M16 19h3\"/>","conversations":"<rect x=\"4\" y=\"5\" width=\"13\" height=\"15\" rx=\"2\"/><path d=\"M8 3h10a2 2 0 0 1 2 2v12M7 9h7M7 13h7M7 17h4\"/>","search":"<circle cx=\"10.8\" cy=\"10.8\" r=\"6.4\"/><path d=\"m16 16 4 4M8.5 10.8h4.6M10.8 8.5v4.6\"/>","images":"<rect x=\"3.5\" y=\"4.5\" width=\"17\" height=\"15\" rx=\"2.5\"/><circle cx=\"9\" cy=\"9.5\" r=\"1.7\"/><path d=\"m5 17 5-4 3 2 3.5-4 3.5 4\"/>","tasks":"<rect x=\"4\" y=\"4\" width=\"16\" height=\"17\" rx=\"2.5\"/><path d=\"m7.5 9 1.5 1.5 3-3M13.5 9H17M7.5 15l1.5 1.5 3-3M13.5 15H17\"/>","library":"<path d=\"M5 4.5h11a2 2 0 0 1 2 2v13H7a2 2 0 0 1-2-2z\"/><path d=\"M5 7H3v12a2 2 0 0 0 2 2h13M8.5 9h6M8.5 13h6M8.5 17h4\"/>","plugins":"<path d=\"M9 4v4M15 4v4M8 8h8v3a3 3 0 0 0 3 3h1v3a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2v-3h1a3 3 0 0 0 3-3z\"/><path d=\"M12 12v2\"/>","more":"<circle cx=\"5\" cy=\"12\" r=\"1.2\" fill=\"currentColor\" stroke=\"none\"/><circle cx=\"12\" cy=\"12\" r=\"1.2\" fill=\"currentColor\" stroke=\"none\"/><circle cx=\"19\" cy=\"12\" r=\"1.2\" fill=\"currentColor\" stroke=\"none\"/>","chevron":"<path d=\"m6 9 6 6 6-6\"/>","location":"<path d=\"M19 10c0 5-7 11-7 11S5 15 5 10a7 7 0 1 1 14 0Z\"/><circle cx=\"12\" cy=\"10\" r=\"2.2\"/><path d=\"M12 3v2\"/>","personalization":"<path d=\"M12 3 20 8v8l-8 5-8-5V8z\"/><circle cx=\"12\" cy=\"12\" r=\"3\"/><path d=\"M12 3v6M20 8l-5 3M20 16l-5-3M4 16l5-3M4 8l5 3\"/>","instructions":"<path d=\"M6 4h12a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2Z\"/><path d=\"M8 9h8M8 13h8M8 17h5\"/>","refresh":"<path d=\"M19 8a7.5 7.5 0 0 0-13.2-2L4 8\"/><path d=\"M4 4v4h4M5 16a7.5 7.5 0 0 0 13.2 2L20 16\"/><path d=\"M20 20v-4h-4\"/>","menu":"<path d=\"M4 6h16M4 12h16M4 18h16\"/><circle cx=\"4\" cy=\"6\" r=\".8\" fill=\"currentColor\"/><circle cx=\"4\" cy=\"18\" r=\".8\" fill=\"currentColor\"/>","model":"<path d=\"M12 2.8 20 7v10l-8 4.2L4 17V7z\"/><path d=\"m8 9 4-2 4 2-4 2zM8 9v5l4 2 4-2V9M12 11v5\"/>","check":"<path d=\"m4.5 12.5 5 5L20 7\"/>","back":"<path d=\"M19 12H5M11 6l-6 6 6 6\"/>","settings":"<path d=\"M12 3.5v2M12 18.5v2M3.5 12h2M18.5 12h2M6 6l1.4 1.4M16.6 16.6 18 18M18 6l-1.4 1.4M7.4 16.6 6 18\"/><circle cx=\"12\" cy=\"12\" r=\"5.2\"/><circle cx=\"12\" cy=\"12\" r=\"1.8\"/>","welcome":"<path d=\"M12 2.5 14.5 9.5 21.5 12l-7 2.5-2.5 7-2.5-7L2.5 12l7-2.5z\"/><circle cx=\"12\" cy=\"12\" r=\"2.2\"/>","learn":"<path d=\"M4 5.5c3.2-1.4 5.9-.9 8 1.1 2.1-2 4.8-2.5 8-1.1v12c-3.2-1.4-5.9-.9-8 1.1-2.1-2-4.8-2.5-8-1.1z\"/><path d=\"M12 6.6v12M7 9h2M7 12h2M15 9h2M15 12h2\"/>","plan":"<path d=\"M4 17.5 17.5 4M8 4h10v10\"/><path d=\"M4 8v12h12\"/>","write":"<path d=\"m4 16.5-.8 4.3 4.3-.8L19 8.5 15.5 5z\"/><path d=\"m13.8 6.7 3.5 3.5M4 4h6\"/>","attach":"<path d=\"m8 12.5 6.8-6.8a3.2 3.2 0 0 1 4.5 4.5l-8.5 8.5a5 5 0 0 1-7.1-7.1l8.1-8.1\"/><path d=\"m7.5 14 7.8-7.8\"/>","send":"<path d=\"M3 11.5 21 3l-5.5 18-3.2-7.3z\"/><path d=\"M12.3 13.7 21 3\"/>","stop":"<rect x=\"5\" y=\"5\" width=\"14\" height=\"14\" rx=\"2\"/>","file":"<path d=\"M6 3.5h8l4 4V20a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 6 20z\"/><path d=\"M14 3.5V8h4M9 13h6M9 16.5h6\"/>","copy":"<rect x=\"8\" y=\"7\" width=\"11\" height=\"13\" rx=\"2\"/><path d=\"M16 7V5a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v11a2 2 0 0 0 2 2h2\"/>","regenerate":"<path d=\"M19 8a7.5 7.5 0 0 0-13.2-2L4 8\"/><path d=\"M4 4v4h4M5 16a7.5 7.5 0 0 0 13.2 2L20 16\"/><path d=\"M20 20v-4h-4\"/>","tsukuyomi":"<path d=\"M16.8 3.5a8.5 8.5 0 1 0 3.7 15.8A9.5 9.5 0 0 1 16.8 3.5Z\"/><path d=\"m7 8 1.2 2.4L11 11l-2.8.8L7 14l-1.1-2.2L3.5 11l2.4-.6z\"/>","apollo":"<circle cx=\"12\" cy=\"12\" r=\"8.5\"/><path d=\"m12 5 2.1 5.1L19 12l-4.9 1.9L12 19l-2.1-5.1L5 12l4.9-1.9z\"/>","mokosh":"<path d=\"M12 3 20 8v8l-8 5-8-5V8z\"/><path d=\"M12 3v18M4 8l16 8M20 8 4 16\"/>","command":"<path d=\"M8 4H6a2 2 0 0 0-2 2v2M16 4h2a2 2 0 0 1 2 2v2M4 16v2a2 2 0 0 0 2 2h2M20 16v2a2 2 0 0 1-2 2h-2\"/><path d=\"M9 9h6v6H9z\"/>","trash":"<path d=\"M4 7h16M9 7V4h6v3M7 7l1 13h8l1-13\"/><path d=\"M10 11v5M14 11v5\"/>","new-task":"<rect x=\"4\" y=\"4\" width=\"13\" height=\"16\" rx=\"2\"/><path d=\"M8 9h5M8 13h3M19 14v6M16 17h6\"/>"};
  function svgIcon(name, className = 'ui-icon') {
    const paths = iconPaths[name];
    if (!paths) return '';
    return '<svg class="' + className + '" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" focusable="false">' + paths + '</svg>';
  }
  function hydrateIcons(root = document) {
    root.querySelectorAll('[data-icon]').forEach(node => {
      const name = node.dataset.icon;
      node.innerHTML = svgIcon(name, 'ui-icon ui-icon-' + name);
      node.removeAttribute('data-icon');
    });
  }
  function id() { return (crypto.randomUUID ? crypto.randomUUID() : Date.now().toString(36) + Math.random().toString(36).slice(2)); }
  function now() { return new Date().toISOString(); }
  function formatTime(value) { try { return new Intl.DateTimeFormat('pt-BR', { hour: '2-digit', minute: '2-digit' }).format(new Date(value)); } catch { return ''; } }
  function titleFrom(text) { const clean = text.replace(/\s+/g, ' ').trim(); return clean.length > 34 ? clean.slice(0, 34).trimEnd() + '…' : clean || 'Nova conversa'; }
  function persist() {
    try {
      const payload = JSON.stringify({ conversations: state.conversations, activeId: state.activeId, theme: state.theme, displayName: state.displayName, generation: state.generation, model: state.model, tasks: state.tasks, projects: state.projects, instructions: state.instructions, location: state.location, aboutMe: state.aboutMe, responseStyle: state.responseStyle, notifications: state.notifications, libraryItems: state.libraryItems, pendingResourceDeletions: state.pendingResourceDeletions, pendingConversationDeletions: state.pendingConversationDeletions });
      if (payload.length > MAX_LOCAL_BYTES) { toast('O histórico local está cheio. Exclua conversas antigas.'); return; }
      localStorage.setItem(STORAGE_KEY, payload);
    } catch (error) { toast('Não foi possível salvar no navegador. Verifique o espaço disponível.'); }
  }
  function restore() {
    try {
      const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}');
      if (Array.isArray(saved.conversations)) state.conversations = saved.conversations.filter(c => c && typeof c.id === 'string' && Array.isArray(c.messages)).slice(0, 200);
      state.activeId = saved.activeId || null;
      state.theme = ['dark','light','system'].includes(saved.theme) ? saved.theme : 'dark';
      state.apiUrl = AUTH_API_URL;
      state.displayName = typeof saved.displayName === 'string' ? saved.displayName.slice(0,32) : 'Visitante';
      state.generation = saved.generation === 'G1' ? 'G1' : 'G1';
      state.model = 'Tsukuyomi';
      state.tasks = Array.isArray(saved.tasks) ? saved.tasks.slice(0,100).map(task => ({...task,id:task.id || id()})) : [];
      state.projects = Array.isArray(saved.projects) ? saved.projects.filter(p => p && typeof p.id === 'string' && typeof p.name === 'string').map(p => ({...p,updatedAt:p.updatedAt || p.createdAt || new Date(0).toISOString()})).slice(0,100) : [];
      state.instructions = typeof saved.instructions === 'string' ? saved.instructions.slice(0,4000) : '';
      state.location = typeof saved.location === 'string' ? saved.location.slice(0,120) : '';
      state.aboutMe = typeof saved.aboutMe === 'string' ? saved.aboutMe.slice(0,1000) : '';
      state.responseStyle = ['conciso','equilibrado','detalhado'].includes(saved.responseStyle) ? saved.responseStyle : 'equilibrado';
      state.notifications = saved.notifications === true && typeof Notification !== 'undefined' && Notification.permission === 'granted';
      state.libraryItems = Array.isArray(saved.libraryItems) ? saved.libraryItems.slice(0,100) : [];
      state.pendingResourceDeletions = Array.isArray(saved.pendingResourceDeletions) ? saved.pendingResourceDeletions.filter(x => x && typeof x.id === 'string' && typeof x.type === 'string' && typeof x.accountId === 'string') : [];
      state.pendingConversationDeletions = Array.isArray(saved.pendingConversationDeletions) ? saved.pendingConversationDeletions.filter(x => x && typeof x.id === 'string' && typeof x.accountId === 'string') : [];
      try { state.authSession = JSON.parse(localStorage.getItem('morok.auth.session') || sessionStorage.getItem('morok.auth.session') || 'null'); } catch { state.authSession = null; }
    } catch { localStorage.removeItem(STORAGE_KEY); }
    if (!state.conversations.some(c => c.id === state.activeId)) state.activeId = null;
  }
  function activeConversation() { return state.conversations.find(c => c.id === state.activeId) || null; }
  function createConversation() {
    const conversation = { id: id(), title: 'Nova conversa', createdAt: now(), updatedAt: now(), messages: [] };
    state.conversations.unshift(conversation);
    state.activeId = conversation.id;
    persist(); renderHistory(); renderConversation(); closeSidebar(); returnToChat(); el.input.focus();
    return conversation;
  }
  function ensureConversation() { return activeConversation() || createConversation(); }
  function applyTheme() {
    let theme = state.theme;
    if (theme === 'system') theme = matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
    document.documentElement.dataset.theme = theme;
    $('meta[name="theme-color"]').content = theme === 'light' ? '#f7f7fa' : '#101014';
  }
  function renderHistory(filter = '') {
    const query = filter.trim().toLocaleLowerCase('pt-BR');
    const list = state.conversations.filter(c => !query || c.title.toLocaleLowerCase('pt-BR').includes(query) || c.messages.some(m => (m.content || '').toLocaleLowerCase('pt-BR').includes(query)));
    $('#history-count').textContent = String(state.conversations.length);
    el.history.replaceChildren();
    if (!list.length) {
      const empty = document.createElement('div'); empty.className = 'history-empty';
      empty.textContent = query ? 'Nenhuma conversa corresponde à busca.' : 'Suas conversas aparecerão aqui.';
      el.history.append(empty); return;
    }
    for (const conversation of list) {
      const item = document.createElement('div'); item.className = 'history-item' + (conversation.id === state.activeId ? ' selected' : ''); item.dataset.id = conversation.id;
      const title = document.createElement('button'); title.type = 'button'; title.className = 'history-title'; title.textContent = conversation.title; title.title = conversation.title;
      title.addEventListener('click', () => openConversation(conversation.id));
      const menu = document.createElement('button'); menu.type = 'button'; menu.className = 'history-menu'; menu.innerHTML = svgIcon('more'); menu.title = 'Opções da conversa'; menu.setAttribute('aria-label', 'Opções de ' + conversation.title);
      menu.addEventListener('click', event => { event.stopPropagation(); openContextMenu(event, conversation.id); });
      item.append(title, menu); el.history.append(item);
    }
  }
  function safeLink(url) {
    try { const parsed = new URL(url); return ['http:','https:'].includes(parsed.protocol) ? parsed.href : null; } catch { return null; }
  }
  function renderMarkdown(text) {
    const blocks = String(text).replace(/\r\n?/g,'\n').split(/\n{2,}/);
    return blocks.map(block => {
      const escaped = block.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
      if (escaped.startsWith('```')) {
        const code = escaped.replace(/^```[^\n]*\n?/,'').replace(/\n?```$/,'');
        return '<pre><code>' + code + '</code></pre>';
      }
      let html = escaped.replace(/`([^`]+)`/g,'<code>$1</code>')
        .replace(/\*\*([^*]+)\*\*/g,'<strong>$1</strong>')
        .replace(/\*([^*]+)\*/g,'<em>$1</em>')
        .replace(/\[([^\]]+)\]\((https?:\/\/[^\s)]+)\)/g, (whole,label,url) => { const link = safeLink(url); return link ? '<a href="' + link + '" target="_blank" rel="noopener noreferrer">' + label + '</a>' : label; })
        .replace(/\n/g,'<br>');
      return '<p>' + html + '</p>';
    }).join('');
  }
  function addMessage(message) {
    const article = document.createElement('article');
    article.className = 'message ' + (message.role === 'user' ? 'user' : 'assistant') + (message.error ? ' message-error' : '');
    article.dataset.messageId = message.id;
    const avatar = document.createElement('div'); avatar.className = 'message-avatar'; if (message.role === 'user') avatar.textContent = initials(state.displayName); else avatar.innerHTML = svgIcon('brand');
    const content = document.createElement('div'); content.className = 'message-content';
    const meta = document.createElement('div'); meta.className = 'message-meta';
    const name = document.createElement('span'); name.className = 'message-name'; name.textContent = message.role === 'user' ? (state.displayName || 'Você') : 'Morok';
    const time = document.createElement('span'); time.className = 'message-time'; time.textContent = formatTime(message.createdAt);
    meta.append(name,time);
    const text = document.createElement('div'); text.className = 'message-text'; text.innerHTML = message.role === 'assistant' && !message.error ? renderMarkdown(message.content) : escapeHtml(message.content).replace(/\n/g,'<br>');
    content.append(meta,text);
    if (message.attachments?.length) {
      const files = document.createElement('div'); files.className = 'message-attachments';
      message.attachments.forEach(file => { const chip = document.createElement('div'); chip.className = 'message-file'; chip.innerHTML = svgIcon('file') + '<span>' + escapeHtml(file.name) + '</span>';  files.append(chip); });
      content.append(files);
    }
    if (message.role === 'assistant' && !message.error) {
      const actions = document.createElement('div'); actions.className = 'message-actions';
      const copy = document.createElement('button'); copy.className = 'message-action'; copy.innerHTML = svgIcon('copy') + '<span>Copiar</span>';  copy.addEventListener('click', () => copyText(message.content));
      actions.append(copy);
      const conversation = activeConversation();
      if (conversation && conversation.messages.at(-1)?.id === message.id) {
        const retry = document.createElement('button'); retry.className = 'message-action'; retry.innerHTML = svgIcon('regenerate') + '<span>Gerar novamente</span>';  retry.addEventListener('click', regenerateLast); actions.append(retry);
      }
      content.append(actions);
    }
    article.append(avatar,content); el.conversation.append(article);
  }
  function escapeHtml(value) { return String(value).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#39;'); }
  function initials(name) { return (name || 'V').trim().split(/\s+/).slice(0,2).map(part => part[0] || '').join('').toUpperCase() || 'V'; }
  function renderConversation() {
    const conversation = activeConversation();
    const hasMessages = !!conversation?.messages.length;
    el.welcome.hidden = hasMessages;
    el.conversation.classList.toggle('visible', hasMessages);
    el.conversation.replaceChildren();
    if (conversation) conversation.messages.forEach(addMessage);
    el.conversation.scrollTop = el.conversation.scrollHeight;
    if (!hasMessages) { el.welcome.hidden = false; el.conversation.classList.remove('visible'); }
  }
  function openConversation(conversationId) {
    if (!state.conversations.some(c => c.id === conversationId)) return;
    state.activeId = conversationId; persist(); renderHistory(); renderConversation(); closeSidebar(); returnToChat();
  }
  function updateProfile() {
    const user = state.authSession?.user;
    const signedIn = !!user?.email;
    $('#profile-name').textContent = signedIn ? (user.name || user.email) : (state.displayName || 'Visitante');
    $('#profile-avatar').textContent = signedIn ? initials(user.name || user.email) : initials(state.displayName);
    $('#profile-subtitle').textContent = signedIn ? 'Conta autenticada' : 'Perfil local · visitante';
    $('#profile-menu-name').textContent = signedIn ? (user.name || user.email) : (state.displayName || 'Visitante');
    $('#profile-menu-email').textContent = signedIn ? user.email : 'Sem sessão autenticada';
    $('#profile-account-action').textContent = signedIn ? 'Minha conta' : 'Entrar ou criar conta';
    $('#profile-local-action').hidden = signedIn;
    $('#profile-menu-logout').hidden = !signedIn;
    $('#profile-menu-insight').textContent = signedIn ? 'Gerencie sua conta e as preferências deste dispositivo.' : 'Você está usando o Morok como visitante. Entre para ativar os recursos vinculados à conta.';
    $('#auth-open').textContent = signedIn ? 'Gerenciar sessão' : 'Entrar ou criar conta';
    $('#profile-button').classList.toggle('signed-in', signedIn);
  }
  function toast(message) {
    el.toast.textContent = message; el.toast.classList.add('show');
    clearTimeout(state.toastTimer); state.toastTimer = setTimeout(() => el.toast.classList.remove('show'), 2600);
  }
  function copyText(text) {
    if (navigator.clipboard?.writeText) navigator.clipboard.writeText(text).then(() => toast('Texto copiado.')).catch(() => toast('Não foi possível copiar o texto.'));
    else { const area = document.createElement('textarea'); area.value = text; document.body.append(area); area.select(); try { document.execCommand('copy'); toast('Texto copiado.'); } catch { toast('Não foi possível copiar.'); } area.remove(); }
  }
  function openSidebar() { el.sidebar.classList.add('open'); el.backdrop.classList.add('show'); }
  function closeSidebar() { el.sidebar.classList.remove('open'); el.backdrop.classList.remove('show'); }
  function openContextMenu(event, conversationId) {
    state.contextId = conversationId;
    const rect = event.currentTarget.getBoundingClientRect();
    el.context.hidden = false;
    const width = 165, height = 83;
    el.context.style.left = Math.min(window.innerWidth - width - 8, Math.max(8, rect.right - width)) + 'px';
    el.context.style.top = Math.min(window.innerHeight - height - 8, rect.bottom + 3) + 'px';
  }
  function closeContextMenu() { el.context.hidden = true; state.contextId = null; }
  function askConfirmation(message, title = 'Confirmar ação') {
    $('#confirm-title').textContent = title;
    $('#confirm-message').textContent = message;
    el.confirm.showModal();
    return new Promise(resolve => { state.confirmResolve = resolve; });
  }
  function finishConfirmation(accepted) {
    const resolve = state.confirmResolve;
    state.confirmResolve = null;
    if (el.confirm.open) el.confirm.close();
    if (resolve) resolve(accepted);
  }
  function renderSearchResults(query = '') {
    const root = $('#conversation-search-results');
    const term = query.trim().toLocaleLowerCase('pt-BR');
    const matches = state.conversations.filter(conversation =>
      !term || conversation.title.toLocaleLowerCase('pt-BR').includes(term) ||
      conversation.messages.some(message => String(message.content || '').toLocaleLowerCase('pt-BR').includes(term))
    );
    root.replaceChildren();
    if (!matches.length) {
      const empty = document.createElement('div');
      empty.className = 'workspace-empty';
      empty.textContent = term ? 'Nenhuma conversa encontrada.' : 'Digite para pesquisar suas conversas.';
      root.append(empty);
      return;
    }
    for (const conversation of matches) {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'search-result';
      const title = document.createElement('strong');
      title.textContent = conversation.title || 'Nova conversa';
      const preview = document.createElement('small');
      preview.textContent = conversation.messages.at(-1)?.content?.slice(0,100) || 'Conversa vazia';
      button.append(title, preview);
      button.addEventListener('click', () => { el.search.close(); openConversation(conversation.id); });
      root.append(button);
    }
  }
  function openSearch() {
    $('#conversation-search-input').value = '';
    renderSearchResults('');
    el.search.showModal();
    $('#conversation-search-input').focus();
  }
  function renameConversation(conversationId) {
    const conversation = state.conversations.find(c => c.id === conversationId); if (!conversation) return;
    state.renameId = conversationId; $('#rename-input').value = conversation.title; closeContextMenu(); el.rename.showModal(); $('#rename-input').focus();
  }
  function deleteConversation(conversationId) {
    const conversation = state.conversations.find(c => c.id === conversationId); if (!conversation) return;
    void askConfirmation('Excluir a conversa "' + conversation.title + '" deste dispositivo?', 'Excluir conversa').then(accepted => {
      if (!accepted) return;
      state.conversations = state.conversations.filter(c => c.id !== conversationId);
      if (state.activeId === conversationId) state.activeId = state.conversations[0]?.id || null;
      persist(); renderHistory(); renderConversation();
      void deleteRemoteConversation(conversationId);
      toast('Conversa excluída deste dispositivo.');
    });
  }
  function resizeInput() {
    el.input.style.height = 'auto'; el.input.style.height = Math.min(el.input.scrollHeight, 170) + 'px';
    el.send.disabled = !el.input.value.trim() && !state.busy;
  }
  function renderAttachments() {
    el.tray.replaceChildren(); el.tray.hidden = state.attachments.length === 0;
    state.attachments.forEach((file,index) => {
      const chip = document.createElement('div'); chip.className = 'attachment-chip';
      const name = document.createElement('span'); name.textContent = file.name;
      const remove = document.createElement('button'); remove.type = 'button'; remove.innerHTML = svgIcon('close'); remove.setAttribute('aria-label','Remover ' + file.name);
      remove.addEventListener('click', () => { state.attachments.splice(index,1); renderAttachments(); });
      chip.append(name,remove); el.tray.append(chip);
    });
  }
  async function readTextFile(file) {
    if (file.size > 48 * 1024) throw new Error(file.name + ' excede 48 KB.');
    return await file.text();
  }
  async function sendMessage(textOverride) {
    if (state.busy) { state.controller?.abort(); return; }
    const raw = (textOverride ?? el.input.value).trim();
    if (!raw && !state.attachments.length) return;
    const conversation = ensureConversation();
    const attachments = [...state.attachments];
    let prompt = raw;
    for (const file of attachments) {
      if (/\.(txt|md|csv|json|log)$/i.test(file.name) || file.type.startsWith('text/')) {
        try { prompt += '\n\n[Arquivo: ' + file.name + ']\n' + await readTextFile(file); }
        catch (error) { toast(error.message); return; }
      } else {
        toast('Imagens são exibidas no histórico, mas o endpoint atual aceita apenas texto.');
      }
    }
    if (!prompt.trim()) { toast('Adicione texto para enviar ao modelo.'); return; }
    const userMessage = { id:id(), role:'user', content:raw || 'Arquivo(s) anexado(s)', attachments:attachments.map(f=>({name:f.name,type:f.type,size:f.size})), createdAt:now() };
    conversation.messages.push(userMessage);
    if (conversation.title === 'Nova conversa') conversation.title = titleFrom(raw || attachments[0]?.name || 'Arquivo');
    conversation.updatedAt = now(); state.attachments = []; renderAttachments(); el.input.value = ''; resizeInput();
    persist(); renderHistory(); renderConversation();
    await requestAnswer(conversation, prompt);
  }
  async function requestAnswer(conversation, prompt) {
    if (!state.apiUrl) {
      const message = { id:id(), role:'assistant', error:true, content:'A API do Morok ainda não foi configurada. Abra Configurações e informe o endereço público do servidor. O servidor local do repositório não está acessível pelo GitHub Pages.', createdAt:now() };
      conversation.messages.push(message); conversation.updatedAt = now(); persist(); renderConversation(); return;
    }
    state.busy = true; state.controller = new AbortController(); setBusy(true);
    const pending = { id:'pending', role:'assistant', content:'Conectando ao Morok…', createdAt:now() };
    addMessage(pending); el.conversation.scrollTop = el.conversation.scrollHeight;
    try {
      const base = state.apiUrl.replace(/\/+$/,'');
      const response = await fetch(base + '/v1/chat', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({prompt, max_new_bytes:256}), signal:state.controller.signal, mode:'cors' });
      const data = await response.json().catch(() => ({}));
      if (!response.ok) throw new Error(data.error || 'O servidor respondeu com HTTP ' + response.status + '.');
      if (typeof data.response !== 'string') throw new Error('A resposta da API não contém o campo response.');
      conversation.messages.push({ id:id(), role:'assistant', content:data.response, requestId:data.request_id || null, createdAt:now() });
      if(state.notifications && document.hidden && typeof Notification!=='undefined' && Notification.permission==='granted') new Notification('Morok',{body:'Sua resposta está pronta.'});
    } catch (error) {
      if (error.name === 'AbortError') {
        conversation.messages.push({ id:id(), role:'assistant', error:true, content:'Geração interrompida pelo usuário.', createdAt:now() });
      } else {
        const reason = error instanceof TypeError ? 'Não foi possível acessar a API. Verifique o endereço, HTTPS e a configuração de CORS do servidor.' : error.message;
        conversation.messages.push({ id:id(), role:'assistant', error:true, content:reason, createdAt:now() });
      }
    } finally {
      state.busy = false; state.controller = null; setBusy(false); conversation.updatedAt = now(); persist(); void syncConversation(conversation); renderHistory(); renderConversation();
    }
  }
  function setBusy(busy) {
    el.send.classList.toggle('busy',busy); el.send.querySelector('span').innerHTML = svgIcon(busy ? 'stop' : 'send');
    el.send.setAttribute('aria-label',busy ? 'Interromper geração' : 'Enviar mensagem');
    el.send.disabled = !busy && !el.input.value.trim();
  }
  function regenerateLast() {
    const conversation = activeConversation(); if (!conversation || state.busy) return;
    let lastUserIndex = -1;
    for (let i=conversation.messages.length-1;i>=0;i--) if (conversation.messages[i].role === 'user') { lastUserIndex=i; break; }
    if (lastUserIndex < 0) return;
    const lastUser = conversation.messages[lastUserIndex];
    const prompt = lastUser.content;
    conversation.messages = conversation.messages.slice(0,lastUserIndex+1);
    persist(); renderConversation(); requestAnswer(conversation,prompt);
  }
  async function checkHealth(showToast = false) {
    if (!state.apiUrl) { setConnection('off','API não configurada'); if (showToast) toast('Configure o endereço da API primeiro.'); return false; }
    const endpoint = state.apiUrl.replace(/\/+$/,'') + '/health';
    let lastError = null;
    for (let attempt = 1; attempt <= 3; attempt++) {
      setConnection('wait',attempt === 1 ? 'Verificando API…' : 'Aguardando API…');
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 15000);
      try {
        const response = await fetch(endpoint,{method:'GET',mode:'cors',cache:'no-store',signal:controller.signal});
        const data = await response.json().catch(() => null);
        if (!response.ok || data?.status !== 'ok') throw new Error('Health check inválido (HTTP ' + response.status + ').');
        setConnection('online','API configurada');
        if (showToast) toast('Conexão com o Morok estabelecida.');
        return true;
      } catch (error) {
        lastError = error;
        if (attempt < 3) await new Promise(resolve => setTimeout(resolve, 1200 * attempt));
      } finally {
        clearTimeout(timeoutId);
      }
    }
    const timedOut = lastError?.name === 'AbortError';
    setConnection('error',timedOut ? 'Tempo esgotado ao conectar' : 'API indisponível');
    if (showToast) toast(timedOut ? 'A API não respondeu após três tentativas.' : 'Não foi possível conectar à API. Verifique a origem do site e o CORS.');
    return false;
  }
  function setConnection(status,label) {
    el.status.textContent = label;
    el.dot.className = 'status-dot' + (status === 'online' ? ' online' : status === 'error' ? ' error' : '');
    el.pill.classList.toggle('online',status === 'online');
    el.pill.querySelector('span').textContent = status === 'online' ? 'Conectado' : status === 'wait' ? 'Conectando' : 'Desconectado';
  }
  function enhanceSelects(root = document) {
    root.querySelectorAll('select').forEach(select => {
      if (select.dataset.siteSelect === 'true') return;
      select.dataset.siteSelect = 'true';
      const wrapper = document.createElement('div');
      wrapper.className = 'site-select';
      const trigger = document.createElement('button');
      trigger.type = 'button';
      trigger.className = 'site-select-trigger';
      trigger.setAttribute('aria-haspopup', 'listbox');
      trigger.setAttribute('aria-expanded', 'false');
      const menu = document.createElement('div');
      menu.className = 'site-select-menu';
      menu.setAttribute('role', 'listbox');
      menu.hidden = true;
      const syncLabel = () => { trigger.innerHTML = '<span>' + escapeHtml(select.selectedOptions[0]?.textContent || 'Selecione') + '</span>' + svgIcon('chevron'); };
      [...select.options].forEach(option => {
        const item = document.createElement('button');
        item.type = 'button';
        item.className = 'site-select-option';
        item.textContent = option.textContent;
        item.setAttribute('role', 'option');
        item.setAttribute('aria-selected', String(option.selected));
        item.addEventListener('click', event => {
          event.preventDefault();
          event.stopPropagation();
          select.value = option.value;
          select.dispatchEvent(new Event('change', { bubbles: true }));
          syncLabel();
          menu.querySelectorAll('.site-select-option').forEach(button => button.setAttribute('aria-selected', String(button.textContent === option.textContent)));
          menu.hidden = true;
          trigger.setAttribute('aria-expanded', 'false');
        });
        menu.append(item);
      });
      trigger.addEventListener('click', event => {
        event.preventDefault();
        event.stopPropagation();
        const willOpen = menu.hidden;
        document.querySelectorAll('.site-select-menu').forEach(other => { other.hidden = true; });
        document.querySelectorAll('.site-select-trigger').forEach(other => other.setAttribute('aria-expanded', 'false'));
        menu.hidden = !willOpen;
        trigger.setAttribute('aria-expanded', String(willOpen));
      });
      select.addEventListener('change', syncLabel);
      select.classList.add('site-select-native');
      select.parentNode.insertBefore(wrapper, select);
      wrapper.append(trigger, menu);
      syncLabel();
    });
  }
  function closeSiteSelects() {
    document.querySelectorAll('.site-select-menu').forEach(menu => { menu.hidden = true; });
    document.querySelectorAll('.site-select-trigger').forEach(trigger => trigger.setAttribute('aria-expanded', 'false'));
  }
  function openSettings() {
    $('#theme-select').value = state.theme;
    $('#theme-select').dispatchEvent(new Event('change', { bubbles: true }));
    el.settings.showModal();
  }
  async function saveSettings() {
    state.theme = $('#theme-select').value;
    applyTheme(); persist(); el.settings.close();
    if (state.authSession?.access_token) {
      try {
        await authRequest('/v1/account/profile', {
          token: state.authSession.access_token,
          body: { name: state.authSession.user?.name || state.displayName || 'Usuário', preferences: { theme: state.theme, language: 'pt-BR' } }
        });
        toast('Preferências salvas e sincronizadas.');
      } catch (error) {
        toast('Preferências salvas neste dispositivo; a sincronização falhou: ' + error.message);
      }
    } else toast('Preferências salvas neste dispositivo.');
    return true;
  }
  function escapeHTML(value) { return String(value ?? '').replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char])); }
  function renderProjectsSidebar() {
    const root=$('#projects-list');
    root.innerHTML='<button class="project-create-link" id="create-project-sidebar" type="button">'+svgIcon('new-task')+'<span>Criar projeto</span></button>'+(state.projects.length?state.projects.map(p=>'<button class="project-sidebar-item" type="button" data-project-open="'+escapeHtml(p.id)+'">'+svgIcon('library')+'<span>'+escapeHtml(p.name)+'</span></button>').join(''):'<div class="history-empty">Nenhum projeto criado.</div>');
    $('#create-project-sidebar').addEventListener('click',()=>{state.activeProjectId=null;openWorkspacePage('projects');});
    root.querySelectorAll('[data-project-open]').forEach(button=>button.addEventListener('click',()=>{state.activeProjectId=button.dataset.projectOpen;openWorkspacePage('projects');}));
  }
  function renderProjectsPage() {
    const p=state.projects.find(item=>item.id===state.activeProjectId);
    if(!p)return '<p class="field-help">Crie projetos para agrupar conversas relacionadas. Com uma conta conectada, eles são sincronizados entre dispositivos; como visitante, ficam neste navegador.</p><form class="inline-create" id="project-form"><input class="text-field" id="project-name-input" maxlength="60" placeholder="Nome do projeto" required><button class="primary-button" type="submit">'+svgIcon('new-task')+'<span>Criar projeto</span></button></form><div class="task-list">'+(state.projects.length?state.projects.map(item=>'<button class="project-list-row" type="button" data-project-open="'+escapeHtml(item.id)+'">'+svgIcon('library')+'<span>'+escapeHtml(item.name)+'</span><small>'+state.conversations.filter(c=>c.projectId===item.id).length+' conversas</small></button>').join(''):'<div class="workspace-empty">Nenhum projeto criado.</div>')+'</div>';
    const related=state.conversations.filter(c=>c.projectId===p.id);
    return '<p class="field-help">Associe conversas a este projeto. Remover uma associação não exclui a conversa.</p><div class="inline-create"><input class="text-field" id="project-rename-input" maxlength="60" value="'+escapeHtml(p.name)+'"><button class="secondary-button" id="project-rename" type="button">Renomear</button></div><div class="dialog-footer"><button class="secondary-button" id="project-add-current" type="button">Adicionar conversa atual</button><button class="danger-button" id="project-delete" type="button">Excluir projeto</button></div><h3 class="workspace-subheading">Conversas do projeto</h3><div class="task-list">'+(related.length?related.map(c=>'<div class="project-list-row"><button type="button" data-project-conversation-open="'+escapeHtml(c.id)+'">'+escapeHtml(c.title)+'</button><button class="icon-button" type="button" data-project-remove-conversation="'+escapeHtml(c.id)+'" aria-label="Remover conversa">'+svgIcon('close')+'</button></div>').join(''):'<div class="workspace-empty">Nenhuma conversa associada.</div>')+'</div><button class="secondary-button" id="project-back" type="button">Voltar aos projetos</button>';
  }
  function openWorkspacePage(page) {
    const titles = {images:'Imagens',tasks:'Tarefas',projects:'Projetos',library:'Biblioteca',plugins:'Plugins',locations:'Localizações',personalization:'Personalização',instructions:'Instruções personalizadas',account:'Minha conta',privacy:'Dados e privacidade',security:'Segurança',notifications:'Notificações',help:'Ajuda e suporte',about:'Sobre o Morok'};
    if (!titles[page]) return;
    $('#workspace-title').textContent = titles[page];
    const root = $('#workspace-page');
    if (page === 'about') root.innerHTML = `
      <div class="feature-card about-hero">
        <span class="about-kicker">KORCZAK TECHNOLOGIES APRESENTA</span>
        <h2>Inteligência artificial feita no Brasil, por brasileiros.</h2>
        <p>Morok é um projeto independente criado pela Korczak Technologies com o propósito de desenvolver tecnologia de inteligência artificial no Brasil. Uma ideia concebida e planejada ao longo de anos que agora começa a ganhar forma e a se transformar em realidade.</p>
        <div class="about-tags"><span>Projeto brasileiro</span><span>Desenvolvimento independente</span><span>Visão de longo prazo</span></div>
      </div>
      <section class="feature-card about-section">
        <h2>Uma trajetória que começa a tomar forma</h2>
        <p>O Morok não nasceu de uma iniciativa passageira. É o resultado de anos de planejamento, estudo e amadurecimento de uma visão: construir uma tecnologia própria, desenvolvida aqui, com identidade brasileira e evolução contínua.</p>
        <p>Este é o início de uma jornada de desenvolvimento. Cada etapa representa trabalho de pesquisa, construção e aperfeiçoamento para transformar essa visão em uma plataforma de inteligência artificial cada vez mais capaz.</p>
      </section>
      <section class="feature-card about-section">
        <h2>Dez gerações. Uma evolução contínua.</h2>
        <p>O projeto foi concebido para contar com dez gerações utilizáveis. Elas serão desenvolvidas e disponibilizadas gradualmente, à medida que cada etapa evoluir e estiver pronta para ser apresentada ao público.</p>
        <div class="about-generation-track" aria-label="Dez gerações planejadas do Morok">
          <span>G1</span><span>G2</span><span>G3</span><span>G4</span><span>G5</span><span>G6</span><span>G7</span><span>G8</span><span>G9</span><span>G10</span>
        </div>
        <p class="about-note">As dez gerações representam o horizonte de desenvolvimento do projeto. A apresentação de cada uma ocorrerá progressivamente, sem antecipar capacidades ou datas ainda não anunciadas.</p>
      </section>
      <section class="feature-card about-section">
        <h2>O que orienta o Morok</h2>
        <div class="about-principles">
          <article><strong>Origem brasileira</strong><p>Uma iniciativa criada e desenvolvida no Brasil, valorizando a capacidade tecnológica nacional.</p></article>
          <article><strong>Independência</strong><p>Um projeto próprio, com identidade, direção e caminho de desenvolvimento definidos pela Korczak Technologies.</p></article>
          <article><strong>Evolução responsável</strong><p>Construção gradual, com atenção à qualidade e à maturidade de cada geração antes de sua apresentação.</p></article>
        </div>
      </section>
      <footer class="about-footer"><span>Morok · Um projeto da Korczak Technologies</span><a href="https://github.com/korczaktech/Morok-AI" target="_blank" rel="noopener noreferrer">Conheça o projeto no GitHub</a></footer>`;
    if (page === 'account') root.innerHTML = state.authSession ? '<p class="field-help">Dados do cadastro e sessões vinculadas à sua conta.</p><div class="feature-card"><label class="field-label" for="account-name-input">Nome</label><input class="text-field" id="account-name-input" maxlength="64" value="'+escapeHtml(state.authSession.user?.name || '')+'"><p>'+escapeHtml(state.authSession.user?.email || '')+'</p><button class="primary-button" id="account-profile-save" type="button">Salvar perfil</button></div><h3 class="workspace-subheading">Sessões e dispositivos</h3><div id="account-sessions" class="task-list">Carregando sessões…</div><button class="secondary-button" id="revoke-other-sessions" type="button">Encerrar outras sessões</button><div class="dialog-footer"><button class="danger-button" id="delete-account-open" type="button">Excluir minha conta</button></div>' : '<p class="field-help">Entre para gerenciar seus dados e sessões.</p><button class="primary-button" id="account-auth" type="button">Entrar ou criar conta</button>';
    if (page === 'privacy') root.innerHTML = '<p class="field-help">Controles dos dados mantidos neste navegador. Conversas e preferências locais não são apagadas da conta do servidor por estes controles.</p><div class="feature-card"><strong>Armazenamento local</strong><p>Histórico, tarefas, instruções e preferências deste dispositivo.</p></div><div class="dialog-footer"><button class="secondary-button" id="export-local-data" type="button">Exportar dados locais</button><button class="danger-button" id="delete-local-data" type="button">Apagar dados locais</button></div>';
    if (page === 'security') root.innerHTML = state.authSession ? '<p class="field-help">Altere sua senha, configure autenticação em dois fatores e encerre sessões de outros dispositivos.</p><div class="feature-card"><strong>Alterar senha</strong><label class="field-label" for="current-password">Senha atual</label><input class="text-field" id="current-password" type="password" autocomplete="current-password"><label class="field-label" for="new-password">Nova senha</label><input class="text-field" id="new-password" type="password" minlength="10" autocomplete="new-password"><button class="primary-button" id="change-password" type="button">Atualizar senha</button><button class="secondary-button" id="toggle-2fa" type="button">'+(state.authSession.user?.twoFactorEnabled?'Desativar autenticação em dois fatores':'Ativar autenticação em dois fatores')+'</button><p class="field-help">'+(state.authSession.user?.twoFactorEnabled?'Segundo fator ativado.':'Segundo fator desativado.')+'</p></div><h3 class="workspace-subheading">Sessões ativas</h3><div id="security-sessions" class="task-list">Carregando sessões…</div><button class="secondary-button" id="security-revoke-others" type="button">Encerrar outras sessões</button>' : '<p class="field-help">Entre para acessar os controles de segurança da conta.</p><button class="primary-button" id="security-auth" type="button">Entrar ou criar conta</button>';
    if (page === 'notifications') root.innerHTML = '<p class="field-help">Receba notificações mesmo com o site fechado. Requer conta autenticada, permissão do navegador e chaves VAPID configuradas no servidor.</p><label class="setting-row"><span><strong>Notificações do navegador</strong><small>Avisos locais neste dispositivo.</small></span><input id="notifications-toggle" type="checkbox" '+(state.notifications?'checked':'')+'></label><p class="field-help">'+(typeof Notification === 'undefined' ? 'API de notificações indisponível neste navegador.' : 'Permissão atual: '+Notification.permission)+'</p><div class="dialog-footer"><button class="primary-button" id="save-notifications" type="button">Salvar preferência</button></div>';
    if (page === 'images') root.innerHTML = '<p class="field-help">Imagens adicionadas ficam nesta sessão. A geração por IA está preparada por contrato, mas ainda não há adaptador conectado ao serviço de modelos.</p><label class="upload-card">'+svgIcon('images')+' Adicionar imagens deste dispositivo<input id="workspace-image-input" type="file" accept="image/*" multiple hidden></label><div class="feature-card integration-card"><strong>Geração de imagens</strong><span class="feature-status">Integração pendente</span><p>Contrato reservado: prompt, modelo e tamanho na entrada; URL da imagem e identificador da solicitação na resposta.</p><label class="field-label" for="image-generation-prompt">Descrição da imagem</label><textarea class="text-field settings-textarea" id="image-generation-prompt" maxlength="4000" placeholder="Descreva a imagem que deseja gerar..."></textarea><button class="primary-button" type="button" disabled>Gerar imagem · indisponível</button></div><div class="workspace-empty" '+(state.images.length?'hidden':'')+'>Nenhuma imagem adicionada nesta sessão.</div><div class="workspace-image-grid" id="workspace-image-grid">'+state.images.map((image,index)=>'<div class="workspace-image-item"><img src="'+image.url+'" alt="'+escapeHTML(image.name)+'"><button class="image-remove" type="button" data-image-delete="'+index+'"> '+svgIcon('trash')+'<span>Remover</span></button><small>'+escapeHTML(image.name)+'</small></div>').join('')+'</div>';
    if (page === 'tasks') root.innerHTML = '<p class="field-help">Tarefas vinculadas à conta são sincronizadas entre dispositivos. Sem sessão, permanecem neste navegador.</p><form class="inline-create" id="task-form"><input class="text-field" id="task-input" maxlength="140" placeholder="Descreva uma tarefa..." required><button class="primary-button" type="submit"> '+svgIcon('new-task')+'<span>Adicionar</span></button></form><div class="task-list" id="task-list">'+(state.tasks.length?state.tasks.map((task,index)=>'<div class="task-row"><label><input type="checkbox" data-task-done="'+index+'" '+(task.done?'checked':'')+'><span class="'+(task.done?'task-done':'')+'">'+escapeHTML(task.text)+'</span></label><button class="icon-button" type="button" data-task-delete="'+index+'" aria-label="Excluir tarefa">'+svgIcon('trash')+'</button></div>').join(''):'<div class="workspace-empty">Nenhuma tarefa criada.</div>')+'</div>';
    if (page === 'library') root.innerHTML = '<p class="field-help">Arquivos de texto são armazenados neste navegador (até 48 KB por arquivo) e podem ser baixados novamente. O envio do conteúdo ao modelo permanece desligado até a integração de contexto.</p><label class="upload-card">'+svgIcon('library')+' Adicionar arquivos de texto<input id="library-file-input" type="file" accept=".txt,.md,.csv,.json,.log,text/plain,text/csv,application/json" multiple hidden></label><div class="task-list">'+(state.libraryItems.length?state.libraryItems.map((item,index)=>'<div class="task-row"><span>'+svgIcon('file')+' '+escapeHTML(item.name)+' <small>'+escapeHTML(item.size)+'</small></span><span class="library-actions">'+(typeof item.content==='string'?'<button class="secondary-button" type="button" data-library-download="'+index+'">Baixar</button>':'<small>Metadados antigos</small>')+'<button class="icon-button" type="button" data-library-delete="'+index+'" aria-label="Remover arquivo">'+svgIcon('trash')+'</button></span></div>').join(''):'<div class="workspace-empty">Sua biblioteca está vazia.</div>')+'</div>';
    if (page === 'plugins') root.innerHTML = '<p class="field-help">Os pontos de extensão estão definidos, mas nenhum conector externo ou serviço de geração está sendo chamado por esta página.</p><div class="feature-card integration-card"><strong>Chat · Tsukuyomi G1</strong><span class="feature-status">Conectado</span><p>Endpoint atual: POST /v1/chat. Contrato de entrada: prompt e max_new_bytes.</p></div><div class="feature-card integration-card"><strong>Contexto do modelo</strong><span class="feature-status">Preparado · não conectado</span><p>Campos reservados: perfil, estilo, instruções, localização e arquivos da biblioteca. Nenhum desses dados é enviado ao modelo.</p></div><div class="feature-card integration-card"><strong>Geração de imagens</strong><span class="feature-status">Preparado · não conectado</span><p>Contrato reservado para prompt, modelo, tamanho e URL de imagem.</p></div><div class="feature-card integration-card"><strong>Plugins externos</strong><span class="feature-status">Preparado · não conectado</span><p>Faltam autorização, permissões, credenciais seguras e chamadas no backend.</p></div>';
    if (page === 'locations') root.innerHTML = '<p class="field-help">Defina uma localização de referência. Ela é salva localmente e não será enviada ao modelo até a conexão do contrato de contexto.</p><label class="field-label" for="location-input">Localização</label><input class="text-field" id="location-input" maxlength="120" placeholder="Cidade, estado ou país" value="'+escapeHTML(state.location)+'"><div class="dialog-footer"><button class="secondary-button" id="use-current-location" type="button">Usar minha localização atual</button><button class="primary-button" id="save-location" type="button">Salvar localização</button></div>';
    if (page === 'personalization') root.innerHTML = '<p class="field-help">Preferências salvas neste navegador. A aplicação automática ao modelo está preparada no contrato de contexto, mas permanece desligada até a integração.</p><label class="field-label" for="about-input">O que o Morok deve saber sobre você?</label><textarea class="text-field settings-textarea" id="about-input" maxlength="1000" placeholder="Interesses, contexto e objetivos...">'+escapeHTML(state.aboutMe)+'</textarea><label class="field-label" for="style-select">Estilo de resposta</label><select id="style-select" class="text-field"><option value="conciso" '+(state.responseStyle==='conciso'?'selected':'')+'>Conciso</option><option value="equilibrado" '+(state.responseStyle==='equilibrado'?'selected':'')+'>Equilibrado</option><option value="detalhado" '+(state.responseStyle==='detalhado'?'selected':'')+'>Detalhado</option></select><label class="field-label" for="workspace-theme">Tema</label><select id="workspace-theme" class="text-field"><option value="dark" '+(state.theme==='dark'?'selected':'')+'>Escuro</option><option value="light" '+(state.theme==='light'?'selected':'')+'>Claro</option><option value="system" '+(state.theme==='system'?'selected':'')+'>Sistema</option></select><div class="dialog-footer"><button class="primary-button" id="save-personalization" type="button">Salvar personalização</button></div>';
    if (page === 'instructions') root.innerHTML = '<p class="field-help">Defina orientações para suas conversas. Elas são salvas localmente; o envio ao motor de inferência permanece desligado até a integração do contrato de contexto.</p><label class="field-label" for="instructions-input">Suas instruções</label><textarea class="text-field settings-textarea tall" id="instructions-input" maxlength="4000" placeholder="Como você gostaria que o Morok respondesse?">'+escapeHTML(state.instructions)+'</textarea><div class="dialog-footer"><button class="primary-button" id="save-instructions" type="button">Salvar instruções</button></div>';
    if (page === 'help') root.innerHTML = '<p class="field-help">Abra um chamado vinculado à sua conta. A equipe poderá acompanhar o status quando o atendimento estiver disponível.</p>'+(state.authSession ? '<form class="inline-create" id="support-ticket-form"><input class="text-field" id="support-subject" maxlength="120" placeholder="Assunto" required><textarea class="text-field settings-textarea" id="support-description" maxlength="8000" placeholder="Descreva o problema (mínimo 10 caracteres)" required></textarea><button class="primary-button" type="submit">Enviar chamado</button></form><div id="support-ticket-list" class="task-list">Carregando chamados…</div>' : '<button class="primary-button" id="support-login" type="button">Entrar para abrir chamado</button>');
    if (page === 'projects') root.innerHTML = renderProjectsPage();
    if (page === 'account' && state.authSession) { void loadAccountProfile(); void loadAccountSessions(); }
    if (page === 'security' && state.authSession) { const target = $('#security-sessions'); if (target) { target.id = 'account-sessions'; void loadAccountSessions(); } void loadAccountProfile(); }
    if (page === 'help' && state.authSession) void loadSupportTickets();
    const pageInsights = {
      about: ['Visão do projeto','Esta página reúne especificações públicas e limites conhecidos. Diferencie sempre funcionalidades implementadas, hipóteses de pesquisa e metas ainda não medidas.'],
      account: ['Identidade e sessão','O perfil de conta representa sua autenticação no serviço. Alterar o nome local não modifica o cadastro remoto nem concede permissões adicionais.'],
      privacy: ['Controle dos seus dados','Exportar cria uma cópia dos dados locais disponíveis. A exclusão local não deve ser confundida com remoção de dados mantidos por serviços de conta.'],
      security: ['Estado de proteção','Esta área informa o estado visível da sessão; não substitui controles do servidor, autenticação multifator ou auditoria de dispositivos.'],
      notifications: ['Preferências de aviso','As opções desta tela controlam avisos da interface. Elas não configuram notificações por e-mail, push ou serviços externos.'],
      images: ['Acervo visual da sessão','Os itens adicionados aqui servem para consulta durante esta sessão. A inclusão de um arquivo não significa que ele foi enviado para um gerador de imagens.'],
      tasks: ['Acompanhamento pessoal','Use tarefas para registrar ações concretas e acompanhar sua conclusão. Com uma conta conectada, a lista sincroniza entre dispositivos.'],
      library: ['Referências armazenadas','A biblioteca registra os arquivos selecionados e seus metadados básicos. Ela não indexa o conteúdo nem disponibiliza os arquivos ao modelo.'],
      plugins: ['Extensões e integrações','Um recurso só deve ser considerado conectado quando houver autorização, credenciais e comunicação verificável com o serviço correspondente.'],
      locations: ['Contexto geográfico','A localização salva é uma referência informada por você. Ela não ativa rastreamento contínuo nem representa sua posição em tempo real.'],
      personalization: ['Preferências de comunicação','As preferências descrevem como a interface deve adaptar respostas. Elas não alteram os parâmetros treináveis nem o comportamento interno do modelo.'],
      instructions: ['Orientações de conversa','As instruções ficam locais. O contrato de contexto reserva este campo, mas ele ainda não é enviado ao motor de inferência.'],
      projects: ['Organização de conversas','Crie projetos e associe conversas. Com uma conta conectada, os projetos são sincronizados entre dispositivos.'],
      help: ['Suporte e diagnóstico','Ao relatar um problema, informe a página afetada, a ação realizada e a mensagem de erro. Não inclua senhas, tokens ou dados privados.']
    };
    if (pageInsights[page]) {
      const [heading, detail] = pageInsights[page];
      root.insertAdjacentHTML('beforeend', '<aside class="page-insight"><span>NOTA DE CONTEXTO</span><strong>'+heading+'</strong><p>'+detail+'</p></aside>');
    }
    enhanceSelects(root);
    $('#workspace-page-view').hidden = false;
    $('#workspace').hidden = true;
    $$('.side-action[data-workspace-page]').forEach(item=>item.classList.toggle('active',item.dataset.workspacePage===page));
    closeSidebar();
  }
  function returnToChat() {
    $('#workspace-page-view').hidden = true;
    $('#workspace').hidden = false;
    $$('.side-action').forEach(item=>item.classList.remove('active'));
    closeSidebar();
  }
  function openProfileMenu() {
    const menu = $('#profile-menu');
    updateProfile();
    menu.hidden = !menu.hidden;
  }
  function closeProfileMenu() { $('#profile-menu').hidden = true; }
  function handleProfileAction(action) {
    closeProfileMenu();
    if (action === 'account') openWorkspacePage('account');
    else if (action === 'settings') openSettings();
    else if (action === 'personalization' || action === 'instructions' || action === 'privacy' || action === 'security' || action === 'notifications') openWorkspacePage(action);
    else if (action === 'help') openWorkspacePage('help');
    else if (action === 'about') openWorkspacePage('about');
    else if (action === 'local-profile') { $('#display-name').value = state.displayName; el.profile.showModal(); }
    else if (action === 'logout') { if (state.authSession) void logout(); }
  }
    function addFiles(files) {
    for (const file of files) {
      if (state.attachments.length >= 4) { toast('Limite de quatro arquivos por mensagem.'); break; }
      if (file.size > 48 * 1024) { toast(file.name + ' excede o limite de 48 KB.'); continue; }
      if (!/\.(txt|md|csv|json|log)$/i.test(file.name) && !file.type.startsWith('text/')) { toast('A conversa aceita apenas arquivos de texto: ' + file.name); continue; }
      state.attachments.push(file);
    }
    renderAttachments(); el.file.value = '';
  }


  async function authRequest(path, options={}) {
    const request = {
      method: options.method || 'POST',
      headers: {
        'Content-Type': 'application/json',
        ...(options.token ? { Authorization: 'Bearer ' + options.token } : {})
      },
      body: options.body === undefined ? undefined : JSON.stringify(options.body),
      mode: 'cors',
      cache: 'no-store'
    };
    let response;
    let lastError;
    const attempts = path === '/auth/login' ? 2 : 1;
    for (let attempt = 1; attempt <= attempts; attempt++) {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 35000);
      try {
        response = await fetch(AUTH_API_URL + path, { ...request, signal: controller.signal });
        break;
      } catch (error) {
        lastError = error;
        if (attempt === attempts) {
          const detail = error.name === 'AbortError' ? 'A API demorou mais de 35 segundos para responder.' : 'Não foi possível alcançar a API. Verifique conexão e CORS.';
          const wrapped = new Error(detail);
          wrapped.cause = error;
          throw wrapped;
        }
        await new Promise(resolve => setTimeout(resolve, 1500));
      } finally {
        clearTimeout(timeoutId);
      }
    }
    if (!response) throw lastError || new Error('Falha de comunicação com a API.');
    const data = await response.json().catch(() => ({}));
    if (!response.ok) { const error = new Error(data.error || 'Falha na autenticação (' + response.status + ').'); error.status = response.status; error.two_factor_required = data.two_factor_required === true; throw error; }
    return data;
  }
  async function syncConversation(conversation) {
    const token = state.authSession?.access_token;
    if (!token || !conversation?.id) return;
    try {
      await authRequest('/v1/conversations/sync', {
        token,
        body: { conversation: {
          id: conversation.id, title: conversation.title, projectId: conversation.projectId || null,
          createdAt: conversation.createdAt, updatedAt: conversation.updatedAt,
          messages: conversation.messages || []
        } }
      });
    } catch (error) {
      console.warn('Não foi possível sincronizar a conversa:', error.message);
    }
  }
  async function deleteRemoteConversation(conversationId) {
    const token = state.authSession?.access_token;
    if (!token || !conversationId) return true;
    try {
      await authRequest('/v1/conversations/delete', { token, body: { ids: [conversationId] } });
      return true;
    } catch (error) {
      console.warn('Não foi possível excluir a conversa na nuvem:', error.message);
      const accountId = state.authSession?.user?.id;
      if (accountId && !state.pendingConversationDeletions.some(item => item.id === conversationId && item.accountId === accountId)) {
        state.pendingConversationDeletions.push({ id: conversationId, accountId });
        persist();
      }
      toast('A exclusão na nuvem ficou pendente e será repetida na próxima sincronização.');
      return false;
    }
  }
  async function loadRemoteConversations() {
    const token = state.authSession?.access_token;
    if (!token) return;
    try {
      const accountId = state.authSession?.user?.id;
      const pending = state.pendingConversationDeletions.filter(item => item.accountId === accountId);
      if (pending.length) {
        await authRequest('/v1/conversations/delete', { token, body: { ids: pending.map(item => item.id) } });
        state.pendingConversationDeletions = state.pendingConversationDeletions.filter(item => item.accountId !== accountId);
        persist();
      }
      const data = await authRequest('/v1/conversations', { method: 'GET', token });
      if (!Array.isArray(data.conversations)) return;
      const localById = new Map(state.conversations.map(item => [item.id, item]));
      const deletedAtById = new Map((Array.isArray(data.deleted) ? data.deleted : []).map(item => [item.id, Date.parse(item.deletedAt) || 0]));
      for (const [conversationId, local] of localById) {
        const deletedAt = deletedAtById.get(conversationId);
        const localTime = Date.parse(local.updatedAt || local.createdAt || 0) || 0;
        if (deletedAt !== undefined && localTime <= deletedAt) localById.delete(conversationId);
      }
      const merged = [];
      for (const remote of data.conversations) {
        const local = localById.get(remote.id);
        if (!local || Date.parse(remote.updatedAt) > Date.parse(local.updatedAt)) {
          merged.push(remote);
        } else {
          merged.push(local);
          void syncConversation(local);
        }
        localById.delete(remote.id);
      }
      for (const local of localById.values()) {
        merged.push(local);
        void syncConversation(local);
      }
      state.conversations = merged.sort((a,b) => Date.parse(b.updatedAt) - Date.parse(a.updatedAt)).slice(0,200);
      if (!state.conversations.some(item => item.id === state.activeId)) state.activeId = state.conversations[0]?.id || null;
      persist(); renderHistory(); renderConversation();
    } catch (error) {
      console.warn('Não foi possível carregar as conversas da nuvem:', error.message);
    }
  }
  function openAuth(mode='login') {
    state.authMode=mode; setAuthMode(mode);
    $('#auth-email').value=state.authSession?.user?.email || '';
    $('#auth-password').value='';
    $('#auth-name').value=state.authSession?.user?.name || '';
    $('#auth-message').textContent='';
    $('#auth-logout').hidden=!state.authSession;
    $('#auth-submit').hidden=!!state.authSession;
    $('#auth-email').disabled=!!state.authSession;
    $('#auth-name').hidden=!!state.authSession || mode!=='signup';
    $('#auth-name').disabled=!!state.authSession || mode!=='signup';
    $('#auth-name-label').hidden=!!state.authSession || mode!=='signup';
    $('#auth-password').hidden=!!state.authSession;
    $$('.auth-tab').forEach(tab=>tab.hidden=!!state.authSession);
    $('#auth-title').textContent=state.authSession?'Conta conectada':'Entrar na sua conta';
    el.auth.showModal();
  }
  function setAuthMode(mode) {
    state.authMode=mode;
    $$('.auth-tab').forEach(tab=>tab.classList.toggle('active',tab.dataset.authMode===mode));
    $('#auth-title').textContent=mode==='signup'?'Criar sua conta':'Entrar na sua conta';
    $('#auth-submit').textContent=mode==='signup'?'Criar conta':'Entrar';
    $('#auth-name').hidden=mode!=='signup';
    $('#auth-name').disabled=mode!=='signup';
    $('#auth-name-label').hidden=mode!=='signup';
    $('#auth-name').required=mode==='signup';
    $('#auth-password').autocomplete=mode==='signup'?'new-password':'current-password';
  }
  function saveAuthSession(session) {
    if (session?.access_token && session?.expires_in) {
      session.expires_at = Math.floor(Date.now()/1000) + Number(session.expires_in);
    }
    state.authSession=session;
    try { if(session) { localStorage.setItem('morok.auth.session',JSON.stringify(session)); sessionStorage.removeItem('morok.auth.session'); } else { localStorage.removeItem('morok.auth.session'); sessionStorage.removeItem('morok.auth.session'); } } catch {}
    if(session?.user?.name) state.displayName=session.user.name.slice(0,32);
    else if(session?.user?.email) state.displayName=session.user.email.split('@')[0].slice(0,32);
    updateProfile(); persist();
  }

  async function deleteRemoteResource(type, resourceId) {
    const token = state.authSession?.access_token;
    if (!token || !resourceId) return true;
    try {
      await authRequest("/v1/resources/delete", { token, body: { type, ids: [resourceId] } });
      return true;
    } catch (error) {
      console.warn("Não foi possível remover o recurso remoto:", error.message);
      const accountId = state.authSession?.user?.id;
      if (accountId && !state.pendingResourceDeletions.some(item => item.type === type && item.id === resourceId && item.accountId === accountId)) {
        state.pendingResourceDeletions.push({ type, id: resourceId, accountId });
        persist();
      }
      toast("A exclusão na nuvem ficou pendente e será repetida na próxima sincronização.");
      return false;
    }
  }
  async function syncAccountResources() {
    const token = state.authSession?.access_token;
    if (!token) return;
    for (const type of ["projects", "tasks"]) {
      const localItems = type === "projects" ? state.projects : state.tasks.map((task, index) => ({ ...task, id: task.id || "task-" + index }));
      try {
        const accountId = state.authSession?.user?.id;
        const pending = state.pendingResourceDeletions.filter(item => item.type === type && item.accountId === accountId);
        if (pending.length) {
          await authRequest("/v1/resources/delete", { token, body: { type, ids: pending.map(item => item.id) } });
          state.pendingResourceDeletions = state.pendingResourceDeletions.filter(item => !(item.type === type && item.accountId === accountId));
          persist();
        }
        const remote = await authRequest("/v1/resources?type=" + type, { method: "GET", token });
        const byId = new Map(localItems.map(item => [item.id, item]));
        for (const tombstone of remote.deleted || []) {
          const local = byId.get(tombstone.id);
          const localTime = Date.parse(local?.updatedAt || local?.createdAt || 0) || 0;
          if (local && localTime <= Date.parse(tombstone.deletedAt)) byId.delete(tombstone.id);
        }
        const merged = [];
        for (const item of remote.items || []) {
          const local = byId.get(item.id);
          if (!local || Date.parse(item.updatedAt) > Date.parse(local.updatedAt || 0)) merged.push({ ...item.data, id: item.id, updatedAt: item.updatedAt });
          else { merged.push(local); }
          byId.delete(item.id);
        }
        for (const local of byId.values()) merged.push(local);
        const payload = merged.map(item => ({ id: item.id, updatedAt: item.updatedAt || item.createdAt || new Date(0).toISOString(), data: item }));
        if (payload.length) await authRequest("/v1/resources/sync", { token, body: { type, items: payload } });
        if (type === "projects") state.projects = merged.slice(0, 100);
        else state.tasks = merged.slice(0, 100);
      } catch (error) { console.warn("Falha ao sincronizar " + type + ":", error.message); }
    }
    persist(); renderProjectsSidebar();
  }

  function decodeVapidKey(value) {
    const padding = "=".repeat((4 - value.length % 4) % 4);
    const base64 = (value + padding).replace(/-/g, "+").replace(/_/g, "/");
    const raw = atob(base64);
    return Uint8Array.from(raw, char => char.charCodeAt(0));
  }
  async function clearLocalPushSubscription() {
    if (!("serviceWorker" in navigator)) return;
    try {
      const registration = await navigator.serviceWorker.getRegistration();
      const subscription = await registration?.pushManager.getSubscription();
      if (subscription) {
        if (state.authSession?.access_token) {
          try {
            await authRequest('/v1/push/unsubscribe', {
              token: state.authSession.access_token,
              body: { endpoint: subscription.endpoint }
            });
          } catch (error) {
            console.warn('Não foi possível remover a inscrição push do servidor:', error.message);
          }
        }
        await subscription.unsubscribe();
      }
    } catch (error) {
      console.warn('Não foi possível remover a inscrição push local:', error.message);
    }
    state.notifications = false;
    persist();
  }

  async function configurePush(enabled) {
    if (!state.authSession?.access_token) throw new Error("Entre na conta para ativar notificações entre dispositivos.");
    if (!("serviceWorker" in navigator) || !("PushManager" in window) || typeof Notification === "undefined") throw new Error("Este navegador não oferece suporte a notificações push.");
    const config = await authRequest("/v1/push/config", { method: "GET", token: state.authSession.access_token });
    const registration = await navigator.serviceWorker.register("./sw.js");
    let subscription = await registration.pushManager.getSubscription();
    if (!enabled) {
      if (subscription) {
        await authRequest("/v1/push/unsubscribe", { token: state.authSession.access_token, body: { endpoint: subscription.endpoint } });
        await subscription.unsubscribe();
      }
      return false;
    }
    if (!config.enabled || !config.publicKey) throw new Error("O serviço push ainda não foi configurado no servidor (chaves VAPID ausentes).");
    const permission = Notification.permission === "granted" ? "granted" : await Notification.requestPermission();
    if (permission !== "granted") throw new Error("Permissão de notificações não concedida.");
    if (!subscription) subscription = await registration.pushManager.subscribe({ userVisibleOnly: true, applicationServerKey: decodeVapidKey(config.publicKey) });
    const result = await authRequest("/v1/push/subscribe", { token: state.authSession.access_token, body: { subscription: subscription.toJSON() } });
    if (!result.pushConfigured) throw new Error("A entrega push não está habilitada no servidor.");
    return true;
  }
  async function loadSupportTickets() {
    const target = $("#support-ticket-list"); if (!target || !state.authSession) return;
    try {
      const data = await authRequest("/v1/support/tickets", { method: "GET", token: state.authSession.access_token });
      const tickets = Array.isArray(data.tickets) ? data.tickets : [];
      target.innerHTML = tickets.map(ticket => {
        const created = new Date(ticket.createdAt);
        const createdLabel = Number.isNaN(created.getTime()) ? "Data indisponível" : created.toLocaleString("pt-BR");
        const status = ticket.status || "open";
        const closeButton = status === "open" ? '<button class="secondary-button" type="button" data-close-ticket="' + escapeHtml(ticket.id) + '">Encerrar</button>' : '';
        return '<div class="task-row"><span><strong>' + escapeHtml(ticket.subject) + '</strong><small>' + escapeHtml(status === "closed" ? "Encerrado" : "Aberto") + ' · ' + escapeHtml(createdLabel) + '</small></span>' + closeButton + '</div>';
      }).join("") || '<div class="workspace-empty">Nenhum chamado enviado.</div>';
    } catch(error) {
      target.innerHTML = '<div class="workspace-empty">' + escapeHtml(error.message) + '</div><button class="secondary-button" id="support-tickets-retry" type="button">Tentar novamente</button>';
    }
  }
  async function loadAccountProfile() {
    const token = state.authSession?.access_token;
    if (!token) return;
    try {
      const data = await authRequest("/v1/account/profile", { method: "GET", token });
      if (data.profile) {
        state.authSession.user ||= {};
        if (data.profile.name) {
          state.authSession.user.name = data.profile.name;
          const nameInput = $("#account-name-input"); if (nameInput) nameInput.value = data.profile.name;
        }
        state.authSession.user.twoFactorEnabled = data.profile.twoFactorEnabled === true;
        const remoteTheme = data.profile.preferences?.Tema;
        if (["dark", "light", "system"].includes(remoteTheme)) {
          state.theme = remoteTheme;
          applyTheme();
          const themeSelect = $("#theme-select"); if (themeSelect) themeSelect.value = remoteTheme;
        }
        saveAuthSession(state.authSession);
        const factorButton = $("#toggle-2fa"); if (factorButton) factorButton.textContent = data.profile.twoFactorEnabled ? "Desativar autenticação em dois fatores" : "Ativar autenticação em dois fatores";
      }
    } catch (error) { toast(error.message); }
  }
  async function loadAccountSessions() {
    const token = state.authSession?.access_token;
    const target = $("#account-sessions");
    if (!token || !target) return;
    target.textContent = "Carregando sessões…";
    try {
      const data = await authRequest("/v1/account/sessions", { method: "GET", token });
      target.innerHTML = (data.sessions || []).map(item => {
        const created = new Date(item.createdAt);
        const createdLabel = Number.isNaN(created.getTime()) ? "Data indisponível" : created.toLocaleString("pt-BR");
        const device = String(item.userAgent || "Dispositivo não identificado").slice(0, 250);
        return '<div class="task-row"><span><strong>' + (item.current ? "Este dispositivo" : "Sessão ativa") + '</strong><small>' + escapeHTML(device) + ' · Criada em ' + escapeHTML(createdLabel) + '</small></span>' + (item.current ? '<span class="feature-status">Atual</span>' : '<button class="secondary-button" type="button" data-revoke-session="' + escapeHTML(item.id) + '">Encerrar</button>') + '</div>';
      }).join("") || '<div class="workspace-empty">Nenhuma sessão ativa.</div>';
    } catch (error) {
      target.innerHTML = '<div class="workspace-empty">' + escapeHTML(error.message) + '</div><button class="secondary-button" id="account-sessions-retry" type="button">Tentar novamente</button>';
    }
  }
  async function refreshAuthSession() {
    const session=state.authSession;
    if(!session?.access_token) return;
    if(Number(session.expires_at || 0) <= Date.now()/1000) {
      await clearLocalPushSubscription();
      saveAuthSession(null);
      return;
    }
    try {
      const data=await authRequest('/auth/me',{method:'GET',token:session.access_token});
      if(data.user) { state.authSession.user=data.user; saveAuthSession(state.authSession); await loadRemoteConversations(); await syncAccountResources(); }
    } catch(error) {
      if(error.status===401) { await clearLocalPushSubscription(); saveAuthSession(null); }
      else updateProfile();
    }
  }
  async function submitAuth() {
    const email=$('#auth-email').value.trim(); const password=$('#auth-password').value; const name=$('#auth-name').value.trim();
    const message=$('#auth-message'); message.className='auth-message'; message.textContent='Conectando ao servidor de contas…';
    if(!email || !email.includes('@')) { message.classList.add('error'); message.textContent='Informe um e-mail válido.'; return; }
    if(password.length<8) { message.classList.add('error'); message.textContent='A senha deve ter pelo menos 8 caracteres.'; return; }
    if(state.authMode==='signup' && !name) { message.classList.add('error'); message.textContent='Informe seu nome.'; return; }
    $('#auth-submit').disabled=true;
    try {
      const path=state.authMode==='signup'?'/auth/signup':'/auth/login';
      const data=await authRequest(path,{body:state.authMode==='signup'?{email,password,name}:{email,password}});
      saveAuthSession(data);
      await loadRemoteConversations();
      await syncAccountResources();
      message.classList.add('success');
      message.textContent=state.authMode==='signup'?'Conta criada com sucesso.':'Login realizado.';
      setTimeout(()=>el.auth.close(),500);
    } catch(error) {
      if (error.two_factor_required) {
        const code = prompt('Digite o código de 6 dígitos do aplicativo autenticador:');
        if (code) {
          try {
            const data = await authRequest('/auth/login', { body: { email, password, totp: code.trim() } });
            saveAuthSession(data); await loadRemoteConversations(); await syncAccountResources();
            message.classList.add('success'); message.textContent='Login realizado com autenticação em dois fatores.';
            setTimeout(()=>el.auth.close(),500); return;
          } catch(secondError) { message.classList.add('error'); message.textContent=secondError.message; }
        } else { message.classList.add('error'); message.textContent='Código de autenticação necessário.'; }
      } else { message.classList.add('error'); message.textContent=error.message; }
    }
    finally { $('#auth-submit').disabled=false; }
  }
  async function logout() {
    await clearLocalPushSubscription();
    try { if(state.authSession?.access_token) await authRequest('/auth/logout',{token:state.authSession.access_token}); } catch {}
    saveAuthSession(null); state.displayName='Visitante'; updateProfile(); persist(); if(el.auth.open) el.auth.close(); closeProfileMenu(); toast('Sessão encerrada neste dispositivo.');
  }
  $$('.auth-tab').forEach(tab=>tab.addEventListener('click',()=>setAuthMode(tab.dataset.authMode)));
  $('#auth-form').addEventListener('submit',event=>{if(event.submitter && event.submitter.id!=='auth-submit')return;event.preventDefault();submitAuth();});
  // Keep dialog dismissal independent from form submission handlers.
  $$('.dialog').forEach(dialog=>{
    dialog.querySelectorAll('.dialog-close').forEach(button=>button.addEventListener('click',event=>{event.preventDefault();dialog.close();}));
    dialog.addEventListener('click',event=>{if(event.target===dialog)dialog.close();});
  });
  $('#auth-logout').addEventListener('click',logout);

  $('#auth-recover').addEventListener('click',()=>{void (async()=>{const email=($('#auth-email').value||'').trim()||prompt('Informe o e-mail da sua conta:');if(!email)return;try{const result=await authRequest('/auth/recover',{body:{email}});$('#auth-message').className='auth-message success';$('#auth-message').textContent=result.message||'Se o endereço estiver cadastrado, enviaremos instruções.';}catch(error){$('#auth-message').className='auth-message error';$('#auth-message').textContent=error.message;}})();});
  const resetToken = new URLSearchParams(location.search).get('reset');
  if (resetToken) {
    history.replaceState({}, '', location.pathname + location.hash);
    setTimeout(()=>{void (async()=>{const password=prompt('Defina sua nova senha (mínimo de 10 caracteres):');if(!password)return;try{await authRequest('/auth/reset',{body:{token:resetToken,password}});alert('Senha redefinida. Entre com a nova senha.');openAuth('login');}catch(error){alert(error.message);}})();},300);
  }


  $('#new-chat').addEventListener('click',createConversation);
  $('#brand-home').addEventListener('click',event=>{event.preventDefault();state.activeId=null;persist();renderHistory($('#sidebar-search-input').value);renderConversation();returnToChat();el.input.focus();});
  $('#sidebar-search-input').addEventListener('input',event=>renderHistory(event.target.value));
  $('#history-toggle').addEventListener('click',()=>{const heading=$('#history-toggle');const expanded=heading.getAttribute('aria-expanded')==='true';heading.setAttribute('aria-expanded',String(!expanded));el.history.hidden=expanded;});
  $('#projects-toggle').addEventListener('click',()=>{const heading=$('#projects-toggle');const expanded=heading.getAttribute('aria-expanded')==='true';heading.setAttribute('aria-expanded',String(!expanded));$('#projects-list').hidden=expanded;});
  $('#workspace-page').addEventListener('submit',event=>{if(event.target.id==='support-ticket-form'){event.preventDefault();void (async()=>{try{await authRequest('/v1/support/tickets',{token:state.authSession.access_token,body:{subject:$('#support-subject').value,description:$('#support-description').value}});toast('Chamado enviado.');openWorkspacePage('help');}catch(error){toast(error.message);}})();}if(event.target.id==='project-form'){event.preventDefault();const name=$('#project-name-input').value.trim();if(!name)return;const p={id:id(),name,createdAt:now(),updatedAt:now()};state.projects.unshift(p);state.activeProjectId=p.id;persist();void syncAccountResources();renderProjectsSidebar();openWorkspacePage('projects');}});
  $('#conversation-search-input').addEventListener('input',event=>renderSearchResults(event.target.value));
  $('#confirm-accept').addEventListener('click',()=>finishConfirmation(true));
  $('#confirm-cancel').addEventListener('click',()=>finishConfirmation(false));
  el.confirm.addEventListener('cancel',event=>{event.preventDefault();finishConfirmation(false);});
  el.confirm.addEventListener('close',()=>{if(state.confirmResolve){const resolve=state.confirmResolve;state.confirmResolve=null;resolve(false);}});
  el.confirm.addEventListener('click',event=>{if(event.target===el.confirm)finishConfirmation(false);});
  $('#search-dialog form').addEventListener('submit',event=>event.preventDefault());
  document.addEventListener('contextmenu',event=>{
    event.preventDefault();
    const row=event.target.closest('.history-item');
    const menuButton=row?.querySelector('.history-menu');
    if(menuButton)openContextMenu({currentTarget:menuButton},row.dataset.id);
  });
  $('#menu-toggle').addEventListener('click',openSidebar);
  $('#sidebar-close').addEventListener('click',closeSidebar);
  el.backdrop.addEventListener('click',closeSidebar);
  $('#settings-open').addEventListener('click',openSettings);
  $('#profile-button').addEventListener('click',openProfileMenu);
  $('#profile-menu').addEventListener('click',event=>{const button=event.target.closest('[data-profile-action]');if(button)handleProfileAction(button.dataset.profileAction);});
  document.addEventListener('click',event=>{if(!event.target.closest('#profile-button')&&!event.target.closest('#profile-menu'))closeProfileMenu();});
  document.addEventListener('click',event=>{if(!event.target.closest('.site-select'))closeSiteSelects();});
  $('#more-options-toggle').addEventListener('click',()=>{const submenu=$('#side-submenu');submenu.hidden=!submenu.hidden;$('#more-options-toggle').setAttribute('aria-expanded',String(!submenu.hidden));});
  $$('[data-workspace-page]').forEach(button=>button.addEventListener('click',()=>openWorkspacePage(button.dataset.workspacePage)));
  $('#workspace-page').addEventListener('submit',event=>{if(event.target.id==='task-form'){event.preventDefault();const value=$('#task-input').value.trim();if(value){state.tasks.unshift({id:id(),text:value,done:false,updatedAt:now()});persist();void syncAccountResources();openWorkspacePage('tasks');}}});
  $('#workspace-page').addEventListener('change',event=>{
    if(event.target.matches('[data-task-done]')){const task=state.tasks[Number(event.target.dataset.taskDone)];if(task){task.done=event.target.checked;task.updatedAt=now();persist();void syncAccountResources();openWorkspacePage('tasks');}}
    if(event.target.id==='workspace-image-input'){for(const file of event.target.files){if(!file.type.startsWith('image/'))continue;state.images.push({name:file.name,url:URL.createObjectURL(file)});}openWorkspacePage('images');}
    if(event.target.id==='library-file-input'){void (async()=>{for(const file of event.target.files){if(file.size>MAX_LIBRARY_FILE_BYTES){toast(file.name+' excede 48 KB.');continue;}try{state.libraryItems.unshift({name:file.name,size:Math.max(1,Math.round(file.size/1024))+' KB',type:file.type||'text/plain',content:await file.text()});}catch{toast('Não foi possível ler '+file.name+'.');}}state.libraryItems=state.libraryItems.slice(0,100);persist();openWorkspacePage('library');})();}
  });
  $('#workspace-page').addEventListener('click',async event=>{
    const closeTicket=event.target.closest('[data-close-ticket]');
    if(closeTicket){
      try {
        await authRequest('/v1/support/tickets/close',{token:state.authSession.access_token,body:{id:closeTicket.dataset.closeTicket}});
        await loadSupportTickets();
        toast('Chamado encerrado.');
      } catch(error){toast(error.message);}
    }
    if (event.target.id === 'account-profile-save') { const name=$('#account-name-input').value.trim(); if(!name)return toast('Informe seu nome.'); try { const data=await authRequest('/v1/account/profile',{token:state.authSession.access_token,body:{name,preferences:{theme:state.theme}}}); state.authSession.user.name=data.profile.name; saveAuthSession(state.authSession); toast('Perfil atualizado.'); updateProfile(); } catch(error){toast(error.message);} }
    if (event.target.id === 'toggle-2fa') { if(state.authSession.user?.twoFactorEnabled){const password=prompt('Confirme sua senha atual:');const code=prompt('Digite o código atual do aplicativo autenticador:');if(password&&code){try{await authRequest('/v1/account/2fa/disable',{token:state.authSession.access_token,body:{password,code}});state.authSession.user.twoFactorEnabled=false;saveAuthSession(state.authSession);openWorkspacePage('security');toast('Autenticação em dois fatores desativada.');}catch(error){toast(error.message);}}}else { void (async()=>{try{const setup=await authRequest('/v1/account/2fa/setup',{token:state.authSession.access_token,body:{}});alert('Adicione esta chave ao seu aplicativo autenticador:\n\n'+setup.secret+'\n\nURI: '+setup.otpauth);const code=prompt('Digite o código de 6 dígitos gerado pelo aplicativo:');if(!code)return;await authRequest('/v1/account/2fa/enable',{token:state.authSession.access_token,body:{code:code.trim()}});state.authSession.user.twoFactorEnabled=true;saveAuthSession(state.authSession);openWorkspacePage('security');toast('Autenticação em dois fatores ativada.');}catch(error){toast(error.message);}})();} }
    if (event.target.id === 'change-password') { const currentPassword=$('#current-password').value, newPassword=$('#new-password').value; if(newPassword.length<10)return toast('A nova senha deve ter pelo menos 10 caracteres.'); try { await authRequest('/v1/account/password',{token:state.authSession.access_token,body:{currentPassword,newPassword}}); $('#current-password').value=''; $('#new-password').value=''; toast('Senha alterada. Outras sessões foram encerradas.'); } catch(error){toast(error.message);} }
    if (event.target.id === 'revoke-other-sessions' || event.target.id === 'security-revoke-others') { try { await authRequest('/v1/account/sessions/revoke',{token:state.authSession.access_token,body:{all:true}}); await loadAccountSessions(); toast('Outras sessões encerradas.'); } catch(error){toast(error.message);} }
    const revoke=event.target.closest('[data-revoke-session]'); if(revoke){try{await authRequest('/v1/account/sessions/revoke',{token:state.authSession.access_token,body:{sessionId:revoke.dataset.revokeSession}});await loadAccountSessions();toast('Sessão encerrada.');}catch(error){toast(error.message);}}
    if(event.target.id==='delete-account-open'){void askConfirmation('Excluir permanentemente sua conta e os dados vinculados no servidor? Esta ação não pode ser desfeita.','Excluir conta').then(async ok=>{if(!ok)return;const password=prompt('Confirme sua senha para excluir a conta:');if(password===null)return;try{await clearLocalPushSubscription();await authRequest('/v1/account/delete',{token:state.authSession.access_token,body:{password}});saveAuthSession(null);state.conversations=[];state.projects=[];state.tasks=[];state.pendingResourceDeletions=[];state.pendingConversationDeletions=[];persist();renderHistory();renderConversation();openWorkspacePage('account');toast('Conta e dados remotos excluídos.');}catch(error){toast(error.message);}});}
    if(event.target.id==='support-login')openAuth('login');
    if(event.target.id==='support-tickets-retry')void loadSupportTickets();
    if(event.target.id==='account-sessions-retry')void loadAccountSessions();

    const taskDelete=event.target.closest('[data-task-delete]');if(taskDelete){const removed=state.tasks.splice(Number(taskDelete.dataset.taskDelete),1)[0];persist();if(removed?.id)await deleteRemoteResource('tasks',removed.id);await syncAccountResources();openWorkspacePage('tasks');}
    const libraryDelete=event.target.closest('[data-library-delete]');if(libraryDelete){state.libraryItems.splice(Number(libraryDelete.dataset.libraryDelete),1);persist();openWorkspacePage('library');}
    const libraryDownload=event.target.closest('[data-library-download]');if(libraryDownload){const item=state.libraryItems[Number(libraryDownload.dataset.libraryDownload)];if(item&&typeof item.content==='string'){const url=URL.createObjectURL(new Blob([item.content],{type:item.type||'text/plain'}));const link=document.createElement('a');link.href=url;link.download=item.name;link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}}
    const imageDelete=event.target.closest('[data-image-delete]');if(imageDelete){const image=state.images.splice(Number(imageDelete.dataset.imageDelete),1)[0];if(image?.url)URL.revokeObjectURL(image.url);openWorkspacePage('images');}
    const pluginDetail=event.target.closest('[data-plugin-detail]');if(pluginDetail){const detail=$('[data-plugin-description="'+pluginDetail.dataset.pluginDetail+'"]');if(detail){detail.hidden=!detail.hidden;pluginDetail.textContent=detail.hidden?'Ver detalhes':'Ocultar detalhes';}}
    if(event.target.id==='account-auth'){openAuth('login');}
    if(event.target.id==='edit-local-profile'){$('#display-name').value=state.displayName;el.profile.showModal();}
    if(event.target.id==='security-auth')openAuth('login');
    if(event.target.id==='project-rename'){const p=state.projects.find(item=>item.id===state.activeProjectId);const name=$('#project-rename-input').value.trim();if(p&&name){p.name=name;p.updatedAt=now();persist();void syncAccountResources();renderProjectsSidebar();openWorkspacePage('projects');}}
    if(event.target.id==='project-delete'){const p=state.projects.find(item=>item.id===state.activeProjectId);if(p){void askConfirmation('Excluir o projeto "'+p.name+'" ? As conversas serão mantidas.','Excluir projeto').then(async ok=>{if(!ok)return;state.projects=state.projects.filter(item=>item.id!==p.id);state.conversations.forEach(c=>{if(c.projectId===p.id){delete c.projectId;void syncConversation(c);}});state.activeProjectId=null;persist();await deleteRemoteResource('projects',p.id);await syncAccountResources();renderProjectsSidebar();openWorkspacePage('projects');});}}
    if(event.target.id==='project-add-current'){const c=activeConversation(),p=state.projects.find(item=>item.id===state.activeProjectId);if(!c)toast('Abra uma conversa antes de adicioná-la ao projeto.');else if(p){c.projectId=p.id;persist();void syncConversation(c);openWorkspacePage('projects');}}
    if(event.target.id==='project-back'){state.activeProjectId=null;openWorkspacePage('projects');}
    const projectOpen=event.target.closest('[data-project-open]');if(projectOpen){state.activeProjectId=projectOpen.dataset.projectOpen;openWorkspacePage('projects');return;}
    const projectConversation=event.target.closest('[data-project-conversation-open]');if(projectConversation){openConversation(projectConversation.dataset.projectConversationOpen);return;}
    const projectRemove=event.target.closest('[data-project-remove-conversation]');if(projectRemove){const c=state.conversations.find(item=>item.id===projectRemove.dataset.projectRemoveConversation);if(c){delete c.projectId;void syncConversation(c);}persist();openWorkspacePage('projects');}
    if(event.target.id==='export-local-data'){const payload={conversations:state.conversations,tasks:state.tasks,projects:state.projects,theme:state.theme,displayName:state.displayName,instructions:state.instructions,location:state.location,aboutMe:state.aboutMe,responseStyle:state.responseStyle,libraryItems:state.libraryItems};const blob=new Blob([JSON.stringify(payload,null,2)],{type:'application/json'});const url=URL.createObjectURL(blob);const link=document.createElement('a');link.href=url;link.download='morok-dados-locais.json';link.click();URL.revokeObjectURL(url);toast('Exportação preparada.');}
    if(event.target.id==='delete-local-data'){void askConfirmation('Apagar conversas, tarefas, projetos, instruções, localização, biblioteca e preferências deste navegador? Esta ação não pode ser desfeita.','Apagar dados locais').then(accepted=>{if(!accepted)return;state.conversations=[];state.activeId=null;state.tasks=[];state.projects=[];state.activeProjectId=null;state.instructions='';state.location='';state.aboutMe='';state.libraryItems=[];state.pendingResourceDeletions=[];state.pendingConversationDeletions=[];state.notifications=false;state.displayName='Visitante';state.images.forEach(i=>{if(i.url)URL.revokeObjectURL(i.url);});state.images=[];persist();renderProjectsSidebar();renderHistory();renderConversation();updateProfile();returnToChat();toast('Dados locais apagados.');});}
    if(event.target.id==='save-notifications'){void (async()=>{const requested=$('#notifications-toggle').checked;try{state.notifications=await configurePush(requested);persist();openWorkspacePage('notifications');toast(state.notifications?'Notificações push ativadas.':'Notificações desativadas.');}catch(error){state.notifications=false;persist();toast(error.message);openWorkspacePage('notifications');}})();}
    if(event.target.id==='use-current-location'){if(!navigator.geolocation){toast('Geolocalização não está disponível neste navegador.');}else{const button=event.target;button.disabled=true;navigator.geolocation.getCurrentPosition(position=>{const lat=position.coords.latitude.toFixed(5),lon=position.coords.longitude.toFixed(5);$('#location-input').value='Coordenadas: '+lat+', '+lon;button.disabled=false;toast('Localização obtida. Salve para manter a referência.');},error=>{button.disabled=false;toast(error.code===1?'Permissão de localização negada.':error.code===2?'Localização indisponível.':'Tempo esgotado ao obter localização.');},{enableHighAccuracy:false,timeout:12000,maximumAge:300000});}}
    if(event.target.id==='save-location'){state.location=$('#location-input').value.trim();persist();toast('Localização salva. A integração com modelos permanece desativada.');}
    if(event.target.id==='save-personalization'){state.aboutMe=$('#about-input').value.trim();state.responseStyle=$('#style-select').value;state.theme=$('#workspace-theme').value;applyTheme();persist();toast('Personalização salva.');}
    if(event.target.id==='save-instructions'){state.instructions=$('#instructions-input').value.trim();persist();toast('Instruções salvas neste dispositivo.');}
  });
  $('#profile-save').addEventListener('click',()=>{const name=$('#display-name').value.trim();state.displayName=name||'Visitante';updateProfile();persist();});
  $('#model-select').addEventListener('click',()=>{const pop=$('#model-popover');pop.hidden=!pop.hidden;if(!pop.hidden){$('#generation-list').hidden=false;$('#model-submenu').hidden=true;}$('#model-select').setAttribute('aria-expanded',String(!pop.hidden));});
  function updateModelMenu() {
    state.model = 'Tsukuyomi';
    $('#model-select-label').textContent = state.generation + ' · Tsukuyomi';
    $$('[data-generation]').forEach(button=>button.classList.toggle('selected',button.dataset.generation===state.generation));
    $$('[data-model]').forEach(button=>button.classList.toggle('selected',button.dataset.model===state.model));
  }
  $$('[data-generation]').forEach(button=>button.addEventListener('click',()=>{if(button.disabled)return;state.generation=button.dataset.generation;updateModelMenu();$('#generation-list').hidden=true;$('#model-submenu').hidden=false;persist();}));
  $('#generation-back').addEventListener('click',()=>{$('#model-submenu').hidden=true;$('#generation-list').hidden=false;});
  $$( '[data-model]' ).forEach(button=>button.addEventListener('click',()=>{if(button.dataset.model!=='Tsukuyomi')return;state.model='Tsukuyomi';updateModelMenu();persist();$('#model-popover').hidden=true;$('#model-select').setAttribute('aria-expanded','false');toast('Tsukuyomi selecionado.');}));
  document.addEventListener('click',event=>{if(!event.target.closest('.model-select-wrap')){$('#model-popover').hidden=true;$('#model-select').setAttribute('aria-expanded','false');}if(!event.target.closest('.context-menu')&&!event.target.closest('.history-menu'))closeContextMenu();});
  el.context.addEventListener('click',event=>{const action=event.target.dataset.action;if(action==='rename')renameConversation(state.contextId);if(action==='delete')deleteConversation(state.contextId);closeContextMenu();});
  $('#rename-save').addEventListener('click',()=>{const conversation=state.conversations.find(c=>c.id===state.renameId);if(conversation){conversation.title=$('#rename-input').value.trim()||'Nova conversa';conversation.updatedAt=now();persist();renderHistory();}state.renameId=null;});
  $('#save-settings').addEventListener('click',event=>{event.preventDefault();saveSettings();});
  $('#auth-open').addEventListener('click',()=>{el.settings.close();openAuth('login');});
  $('#status-refresh').addEventListener('click',()=>checkHealth(true));
  $('#clear-data').addEventListener('click',()=>{void askConfirmation('Apagar todas as conversas deste dispositivo? As tarefas, biblioteca e preferências serão mantidas.','Apagar conversas').then(accepted=>{if(!accepted)return;state.conversations=[];state.activeId=null;state.attachments=[];persist();renderHistory();renderConversation();renderAttachments();toast('Conversas apagadas.');});});
  $('#attach-button').addEventListener('click',()=>el.file.click());
  el.file.addEventListener('change',event=>addFiles([...event.target.files]));
  $$('.suggestion').forEach(button=>button.addEventListener('click',()=>{el.input.value=button.dataset.prompt;resizeInput();el.input.focus();}));
  el.input.addEventListener('input',resizeInput);
  el.input.addEventListener('keydown',event=>{if(event.key==='Enter'&&!event.shiftKey){event.preventDefault();if(!state.busy)sendMessage();else state.controller?.abort();}});
  el.composer.addEventListener('submit',event=>{event.preventDefault();sendMessage();});
  document.addEventListener('keydown',event=>{
    if(event.key==='Escape'){closeSidebar();closeContextMenu();closeSiteSelects();$('#model-popover').hidden=true;if(el.search.open)el.search.close();}
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='k'){event.preventDefault();closeSidebar();openSearch();}
    if((event.ctrlKey||event.metaKey)&&event.shiftKey&&event.key.toLowerCase()==='o'){event.preventDefault();createConversation();}
  });
  matchMedia('(prefers-color-scheme: light)').addEventListener?.('change',()=>{if(state.theme==='system')applyTheme();});
  hydrateIcons(); restore(); applyTheme(); renderProjectsSidebar(); renderHistory(); renderConversation(); updateProfile(); updateModelMenu(); refreshAuthSession();
})();