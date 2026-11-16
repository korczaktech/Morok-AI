(() => {
  'use strict';
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const STORAGE_KEY = 'morok.web.state.v2';
  const AUTH_API_URL = 'https://morok-ai.onrender.com';
  const MAX_LOCAL_BYTES = 3_500_000;
  const state = { conversations: [], activeId: null, theme: 'dark', apiUrl: AUTH_API_URL, authSession: null, authMode: 'login', generation: 'G1', model: 'Tsukuyomi', displayName: 'Visitante', attachments: [], images: [], tasks: [], instructions: '', location: '', aboutMe: '', responseStyle: 'equilibrado', notifications: true, libraryItems: [], busy: false, controller: null, contextId: null, renameId: null, confirmResolve: null, toastTimer: null };
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
      const payload = JSON.stringify({ conversations: state.conversations, activeId: state.activeId, theme: state.theme, displayName: state.displayName, generation: state.generation, model: state.model, tasks: state.tasks, instructions: state.instructions, location: state.location, aboutMe: state.aboutMe, responseStyle: state.responseStyle, notifications: state.notifications, libraryItems: state.libraryItems });
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
      state.model = ['Tsukuyomi','Apollo','Mokosh'].includes(saved.model) ? saved.model : 'Tsukuyomi';
      state.tasks = Array.isArray(saved.tasks) ? saved.tasks.slice(0,100) : [];
      state.instructions = typeof saved.instructions === 'string' ? saved.instructions.slice(0,4000) : '';
      state.location = typeof saved.location === 'string' ? saved.location.slice(0,120) : '';
      state.aboutMe = typeof saved.aboutMe === 'string' ? saved.aboutMe.slice(0,1000) : '';
      state.responseStyle = ['conciso','equilibrado','detalhado'].includes(saved.responseStyle) ? saved.responseStyle : 'equilibrado';
      state.notifications = saved.notifications !== false;
      state.libraryItems = Array.isArray(saved.libraryItems) ? saved.libraryItems.slice(0,100) : [];
      try { state.authSession = JSON.parse(localStorage.getItem('morok.auth.session') || sessionStorage.getItem('morok.auth.session') || 'null'); } catch { state.authSession = null; }
    } catch { localStorage.removeItem(STORAGE_KEY); }
    if (!state.conversations.some(c => c.id === state.activeId)) state.activeId = null;
  }
  function activeConversation() { return state.conversations.find(c => c.id === state.activeId) || null; }
  function createConversation() {
    const conversation = { id: id(), title: 'Nova conversa', createdAt: now(), updatedAt: now(), messages: [] };
    state.conversations.unshift(conversation);
    state.activeId = conversation.id;
    persist(); renderHistory(); renderConversation(); closeSidebar(); el.input.focus();
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
    state.activeId = conversationId; persist(); renderHistory(); renderConversation(); closeSidebar();
  }
  function updateProfile() {
    const user = state.authSession?.user;
    const signedIn = !!user?.email;
    $('#profile-name').textContent = signedIn ? user.email : (state.displayName || 'Visitante');
    $('#profile-avatar').textContent = signedIn ? initials(user.email) : initials(state.displayName);
    $('#profile-subtitle').textContent = signedIn ? 'Conta autenticada' : 'Configurar perfil';
    $('#profile-menu-name').textContent = signedIn ? (user.name || user.email) : (state.displayName || 'Visitante');
    $('#profile-menu-email').textContent = signedIn ? user.email : 'Conta local';
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
      persist(); renderHistory(); renderConversation(); toast('Conversa excluída.');
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
  function saveSettings() {
    state.theme = $('#theme-select').value;
    applyTheme(); persist(); el.settings.close(); toast('Preferências salvas.'); return true;
  }
  const workspaceDialog = $('#workspace-dialog');
  function escapeHTML(value) { return String(value ?? '').replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char])); }
  function openWorkspacePage(page) {
    const titles = {images:'Imagens',tasks:'Tarefas',library:'Biblioteca',plugins:'Plugins',locations:'Localizações',personalization:'Personalização',instructions:'Instruções personalizadas',account:'Minha conta',privacy:'Dados e privacidade',security:'Segurança',notifications:'Notificações',help:'Ajuda e suporte'};
    if (!titles[page]) return;
    $('#workspace-title').textContent = titles[page];
    const root = $('#workspace-page');
    if (page === 'account') root.innerHTML = '<p class="field-help">Gerencie sua identidade no Morok. A autenticação é separada das preferências locais.</p><div class="feature-card"><strong>'+(state.authSession?.user?.email ? escapeHTML(state.authSession.user.email) : 'Conta não conectada')+'</strong><p>'+(state.authSession ? 'Sua sessão está autenticada.' : 'Entre ou crie uma conta para acessar recursos de conta.')+'</p><span class="feature-status">'+(state.authSession ? 'Conectado' : 'Visitante')+'</span></div><div class="dialog-footer"><button class="secondary-button" id="edit-local-profile" type="button">Editar nome de exibição</button><button class="primary-button" id="account-auth" type="button">'+(state.authSession?'Gerenciar sessão':'Entrar ou criar conta')+'</button></div>';
    if (page === 'privacy') root.innerHTML = '<p class="field-help">Controles dos dados mantidos neste navegador. Conversas e preferências locais não são apagadas da conta do servidor por estes controles.</p><div class="feature-card"><strong>Armazenamento local</strong><p>Histórico, tarefas, instruções e preferências deste dispositivo.</p></div><div class="dialog-footer"><button class="secondary-button" id="export-local-data" type="button">Exportar dados locais</button><button class="danger-button" id="delete-local-data" type="button">Apagar dados locais</button></div>';
    if (page === 'security') root.innerHTML = '<p class="field-help">Estado da sessão e acesso à conta.</p><div class="feature-card"><strong>Sessão atual</strong><p>'+(state.authSession?.user?.email ? escapeHTML(state.authSession.user.email) : 'Nenhuma conta conectada')+'</p><span class="feature-status">'+(state.authSession ? 'Sessão ativa neste navegador' : 'Sessão não iniciada')+'</span></div><p class="field-help">Alteração de senha, autenticação em dois fatores e dispositivos conectados ainda não são gerenciados por esta interface.</p><div class="dialog-footer"><button class="primary-button" id="security-auth" type="button">'+(state.authSession?'Ver sessão':'Entrar na conta')+'</button></div>';
    if (page === 'notifications') root.innerHTML = '<p class="field-help">Preferências de notificações locais.</p><label class="setting-row"><span><strong>Notificações</strong><small>Permitir avisos dentro desta interface.</small></span><input id="notifications-toggle" type="checkbox" '+(state.notifications?'checked':'')+'></label><div class="dialog-footer"><button class="primary-button" id="save-notifications" type="button">Salvar preferência</button></div>';
    if (page === 'images') root.innerHTML = '<p class="field-help">Organize imagens escolhidas neste dispositivo. Os arquivos ficam disponíveis nesta sessão; geração por IA ainda não está conectada.</p><label class="upload-card">'+svgIcon('images')+' Adicionar imagens deste dispositivo<input id="workspace-image-input" type="file" accept="image/*" multiple hidden></label><div class="workspace-empty" '+(state.images.length?'hidden':'')+'>Nenhuma imagem adicionada nesta sessão.</div><div class="workspace-image-grid" id="workspace-image-grid">'+state.images.map((image,index)=>'<div class="workspace-image-item"><img src="'+image.url+'" alt="'+escapeHTML(image.name)+'"><button class="image-remove" type="button" data-image-delete="'+index+'"> '+svgIcon('trash')+'<span>Remover</span></button><small>'+escapeHTML(image.name)+'</small></div>').join('')+'</div>';
    if (page === 'tasks') root.innerHTML = '<p class="field-help">Crie uma lista de tarefas local. Ela fica salva neste navegador.</p><form class="inline-create" id="task-form"><input class="text-field" id="task-input" maxlength="140" placeholder="Descreva uma tarefa..." required><button class="primary-button" type="submit"> '+svgIcon('new-task')+'<span>Adicionar</span></button></form><div class="task-list" id="task-list">'+(state.tasks.length?state.tasks.map((task,index)=>'<div class="task-row"><label><input type="checkbox" data-task-done="'+index+'" '+(task.done?'checked':'')+'><span class="'+(task.done?'task-done':'')+'">'+escapeHTML(task.text)+'</span></label><button class="icon-button" type="button" data-task-delete="'+index+'" aria-label="Excluir tarefa">'+svgIcon('trash')+'</button></div>').join(''):'<div class="workspace-empty">Nenhuma tarefa criada.</div>')+'</div>';
    if (page === 'library') root.innerHTML = '<p class="field-help">Lista de arquivos adicionados. Por enquanto, apenas nomes e tamanhos são registrados; o conteúdo não é enviado nem armazenado na nuvem.</p><label class="upload-card">'+svgIcon('library')+' Adicionar arquivos<input id="library-file-input" type="file" multiple hidden></label><div class="task-list">'+(state.libraryItems.length?state.libraryItems.map((item,index)=>'<div class="task-row"><span>'+svgIcon('file')+' '+escapeHTML(item.name)+' <small>'+escapeHTML(item.size)+'</small></span><button class="icon-button" type="button" data-library-delete="'+index+'" aria-label="Remover arquivo">'+svgIcon('trash')+'</button></div>').join(''):'<div class="workspace-empty">Sua biblioteca está vazia.</div>')+'</div>';
    if (page === 'plugins') root.innerHTML = '<p class="field-help">Gerencie os recursos disponíveis nesta interface. Conectores externos ainda não estão integrados ao backend do Morok.</p><div class="feature-card"><strong>Biblioteca local</strong><p>Organiza os arquivos que você adiciona neste navegador.</p><span class="feature-status">Disponível nesta sessão</span><button class="secondary-button plugin-detail-toggle" type="button" data-plugin-detail="library">Ver detalhes</button><p class="plugin-detail" data-plugin-description="library" hidden>Use a seção Biblioteca na barra lateral para adicionar e remover referências de arquivos. O conteúdo não é enviado ao servidor.</p></div><div class="feature-card"><strong>Conectores externos</strong><p>Integrações com serviços de terceiros.</p><span class="feature-status">Não conectado</span><button class="secondary-button plugin-detail-toggle" type="button" data-plugin-detail="external">Ver detalhes</button><p class="plugin-detail" data-plugin-description="external" hidden>A conexão OAuth, permissões e chamadas de serviços externos ainda precisam ser implementadas no backend. Nenhuma conta externa foi conectada.</p></div>';
    if (page === 'locations') root.innerHTML = '<p class="field-help">Defina uma localização de referência para experiências que dependam de região. Não usamos sua localização automaticamente.</p><label class="field-label" for="location-input">Localização</label><input class="text-field" id="location-input" maxlength="120" placeholder="Cidade, estado ou país" value="'+escapeHTML(state.location)+'"><div class="dialog-footer"><button class="primary-button" id="save-location" type="button">Salvar localização</button></div>';
    if (page === 'personalization') root.innerHTML = '<p class="field-help">Preferências de personalização salvas neste navegador.</p><label class="field-label" for="about-input">O que o Morok deve saber sobre você?</label><textarea class="text-field settings-textarea" id="about-input" maxlength="1000" placeholder="Interesses, contexto e objetivos...">'+escapeHTML(state.aboutMe)+'</textarea><label class="field-label" for="style-select">Estilo de resposta</label><select id="style-select" class="text-field"><option value="conciso" '+(state.responseStyle==='conciso'?'selected':'')+'>Conciso</option><option value="equilibrado" '+(state.responseStyle==='equilibrado'?'selected':'')+'>Equilibrado</option><option value="detalhado" '+(state.responseStyle==='detalhado'?'selected':'')+'>Detalhado</option></select><label class="field-label" for="workspace-theme">Tema</label><select id="workspace-theme" class="text-field"><option value="dark" '+(state.theme==='dark'?'selected':'')+'>Escuro</option><option value="light" '+(state.theme==='light'?'selected':'')+'>Claro</option><option value="system" '+(state.theme==='system'?'selected':'')+'>Sistema</option></select><div class="dialog-footer"><button class="primary-button" id="save-personalization" type="button">Salvar personalização</button></div>';
    if (page === 'instructions') root.innerHTML = '<p class="field-help">Defina orientações que deseja aplicar ao conversar. Por enquanto, elas ficam salvas localmente e ainda não são enviadas ao motor de inferência.</p><label class="field-label" for="instructions-input">Suas instruções</label><textarea class="text-field settings-textarea tall" id="instructions-input" maxlength="4000" placeholder="Como você gostaria que o Morok respondesse?">'+escapeHTML(state.instructions)+'</textarea><div class="dialog-footer"><button class="primary-button" id="save-instructions" type="button">Salvar instruções</button></div>';
    if (page === 'help') root.innerHTML = '<p class="field-help">Consulte a documentação e os canais oficiais do projeto Morok para obter suporte.</p><a class="secondary-button" href="https://github.com/korczaktech/Morok-AI" target="_blank" rel="noopener noreferrer">Abrir repositório do projeto</a>';
    enhanceSelects(root);
    $('#workspace-page-view').hidden = false;
    $('#workspace').hidden = true;
    $('.side-action[data-workspace-page]').forEach(item=>item.classList.toggle('active',item.dataset.workspacePage===page));
    closeSidebar();
  }
  function returnToChat() {
    $('#workspace-page-view').hidden = true;
    $('#workspace').hidden = false;
    $('.side-action').forEach(item=>item.classList.remove('active'));
    closeSidebar();
  }
  function openProfileMenu() {
    const menu = $('#profile-menu');
    menu.hidden = !menu.hidden;
    $('#profile-menu-name').textContent = state.displayName || 'Visitante';
    $('#profile-menu-email').textContent = state.authSession?.user?.email || 'Conta local';
  }
  function closeProfileMenu() { $('#profile-menu').hidden = true; }
  function handleProfileAction(action) {
    closeProfileMenu();
    if (action === 'account') openWorkspacePage('account');
    else if (action === 'settings') openSettings();
    else if (action === 'personalization' || action === 'instructions' || action === 'privacy' || action === 'security' || action === 'notifications') openWorkspacePage(action);
    else if (action === 'help') openWorkspacePage('help');
    else if (action === 'logout') { if (state.authSession) $('#auth-logout').click(); else toast('Nenhuma conta está conectada.'); }
  }
    function addFiles(files) {
    for (const file of files) {
      if (state.attachments.length >= 4) { toast('Limite de quatro arquivos por mensagem.'); break; }
      if (file.size > 48 * 1024) { toast(file.name + ' excede o limite de 48 KB.'); continue; }
      if (!/\.(txt|md|csv|json|log)$/i.test(file.name) && !file.type.startsWith('image/')) { toast('Formato não suportado: ' + file.name); continue; }
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
    if (!response.ok) { const error = new Error(data.error || 'Falha na autenticação (' + response.status + ').'); error.status = response.status; throw error; }
    return data;
  }
  async function syncConversation(conversation) {
    const token = state.authSession?.access_token;
    if (!token || !conversation?.id) return;
    try {
      await authRequest('/v1/conversations/sync', {
        token,
        body: { conversation: {
          id: conversation.id, title: conversation.title,
          createdAt: conversation.createdAt, updatedAt: conversation.updatedAt,
          messages: conversation.messages || []
        } }
      });
    } catch (error) {
      console.warn('Não foi possível sincronizar a conversa:', error.message);
    }
  }
  async function loadRemoteConversations() {
    const token = state.authSession?.access_token;
    if (!token) return;
    try {
      const data = await authRequest('/v1/conversations', { method: 'GET', token });
      if (!Array.isArray(data.conversations)) return;
      const localById = new Map(state.conversations.map(item => [item.id, item]));
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
  async function refreshAuthSession() {
    const session=state.authSession;
    if(!session?.access_token) return;
    if(Number(session.expires_at || 0) <= Date.now()/1000) {
      saveAuthSession(null);
      return;
    }
    try {
      const data=await authRequest('/auth/me',{method:'GET',token:session.access_token});
      if(data.user) { state.authSession.user=data.user; saveAuthSession(state.authSession); await loadRemoteConversations(); }
    } catch(error) { if(error.status===401) saveAuthSession(null); else updateProfile(); }
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
      message.classList.add('success');
      message.textContent=state.authMode==='signup'?'Conta criada com sucesso.':'Login realizado.';
      setTimeout(()=>el.auth.close(),500);
    } catch(error) { message.classList.add('error'); message.textContent=error.message; }
    finally { $('#auth-submit').disabled=false; }
  }
  async function logout() {
    try { if(state.authSession?.access_token) await authRequest('/auth/logout',{token:state.authSession.access_token}); } catch {}
    saveAuthSession(null); state.displayName='Visitante'; updateProfile(); persist(); openAuth('login'); $('#auth-message').className='auth-message success'; $('#auth-message').textContent='Sessão encerrada neste dispositivo.';
  }
  // Start API health check before optional UI bindings.
  hydrateIcons(); restore(); applyTheme(); renderHistory(); renderConversation(); updateProfile(); updateModelMenu(); enhanceSelects(); checkHealth(); refreshAuthSession();

  $$('.auth-tab').forEach(tab=>tab.addEventListener('click',()=>setAuthMode(tab.dataset.authMode)));
  $('#auth-form').addEventListener('submit',event=>{if(event.submitter && event.submitter.id!=='auth-submit')return;event.preventDefault();submitAuth();});
  // Keep dialog dismissal independent from form submission handlers.
  $$('.dialog').forEach(dialog=>{
    dialog.querySelectorAll('.dialog-close').forEach(button=>button.addEventListener('click',event=>{event.preventDefault();dialog.close();}));
    dialog.addEventListener('click',event=>{if(event.target===dialog)dialog.close();});
  });
  $('#auth-logout').addEventListener('click',logout);

  $('#new-chat').addEventListener('click',createConversation);
  $('#brand-home').addEventListener('click',event=>{event.preventDefault();state.activeId=null;persist();renderHistory($('#sidebar-search-input').value);renderConversation();returnToChat();el.input.focus();});
  $('#sidebar-search-input').addEventListener('input',event=>renderHistory(event.target.value));
  $('#history-toggle').addEventListener('click',()=>{const heading=$('#history-toggle');const collapsed=heading.getAttribute('aria-expanded')==='false';heading.setAttribute('aria-expanded',String(!collapsed));el.history.hidden=collapsed;});
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
  $('[data-workspace-page]').forEach(button=>button.addEventListener('click',()=>openWorkspacePage(button.dataset.workspacePage)));
  $('#workspace-page').addEventListener('submit',event=>{if(event.target.id==='task-form'){event.preventDefault();const value=$('#task-input').value.trim();if(value){state.tasks.unshift({text:value,done:false});persist();openWorkspacePage('tasks');}}});
  $('#workspace-page').addEventListener('change',event=>{
    if(event.target.matches('[data-task-done]')){const task=state.tasks[Number(event.target.dataset.taskDone)];if(task){task.done=event.target.checked;persist();openWorkspacePage('tasks');}}
    if(event.target.id==='workspace-image-input'){for(const file of event.target.files){if(!file.type.startsWith('image/'))continue;state.images.push({name:file.name,url:URL.createObjectURL(file)});}openWorkspacePage('images');}
    if(event.target.id==='library-file-input'){for(const file of event.target.files){state.libraryItems.unshift({name:file.name,size:Math.max(1,Math.round(file.size/1024))+' KB'});}persist();openWorkspacePage('library');}
  });
  $('#workspace-page').addEventListener('click',event=>{
    const taskDelete=event.target.closest('[data-task-delete]');if(taskDelete){state.tasks.splice(Number(taskDelete.dataset.taskDelete),1);persist();openWorkspacePage('tasks');}
    const libraryDelete=event.target.closest('[data-library-delete]');if(libraryDelete){state.libraryItems.splice(Number(libraryDelete.dataset.libraryDelete),1);persist();openWorkspacePage('library');}
    const imageDelete=event.target.closest('[data-image-delete]');if(imageDelete){const image=state.images.splice(Number(imageDelete.dataset.imageDelete),1)[0];if(image?.url)URL.revokeObjectURL(image.url);openWorkspacePage('images');}
    const pluginDetail=event.target.closest('[data-plugin-detail]');if(pluginDetail){const detail=$('[data-plugin-description="'+pluginDetail.dataset.pluginDetail+'"]');if(detail){detail.hidden=!detail.hidden;pluginDetail.textContent=detail.hidden?'Ver detalhes':'Ocultar detalhes';}}
    if(event.target.id==='account-auth'){openAuth('login');}
    if(event.target.id==='edit-local-profile'){$('#display-name').value=state.displayName;el.profile.showModal();}
    if(event.target.id==='security-auth')openAuth('login');
    if(event.target.id==='export-local-data'){const payload={conversations:state.conversations,tasks:state.tasks,theme:state.theme,displayName:state.displayName,instructions:state.instructions,location:state.location,aboutMe:state.aboutMe,responseStyle:state.responseStyle,libraryItems:state.libraryItems};const blob=new Blob([JSON.stringify(payload,null,2)],{type:'application/json'});const url=URL.createObjectURL(blob);const link=document.createElement('a');link.href=url;link.download='morok-dados-locais.json';link.click();URL.revokeObjectURL(url);toast('Exportação preparada.');}
    if(event.target.id==='delete-local-data'){void askConfirmation('Apagar conversas, tarefas, instruções, localização, biblioteca e preferências deste navegador? Esta ação não pode ser desfeita.','Apagar dados locais').then(accepted=>{if(!accepted)return;state.conversations=[];state.activeId=null;state.tasks=[];state.instructions='';state.location='';state.aboutMe='';state.libraryItems=[];state.displayName='Visitante';persist();renderHistory();renderConversation();updateProfile();returnToChat();toast('Dados locais apagados.');});}
    if(event.target.id==='save-notifications'){state.notifications=$('#notifications-toggle').checked;persist();toast('Preferência de notificações salva.');}
    if(event.target.id==='save-location'){state.location=$('#location-input').value.trim();persist();toast('Localização salva.');}
    if(event.target.id==='save-personalization'){state.aboutMe=$('#about-input').value.trim();state.responseStyle=$('#style-select').value;state.theme=$('#workspace-theme').value;applyTheme();persist();toast('Personalização salva.');}
    if(event.target.id==='save-instructions'){state.instructions=$('#instructions-input').value.trim();persist();toast('Instruções salvas neste dispositivo.');}
  });
  $('#profile-save').addEventListener('click',()=>{const name=$('#display-name').value.trim();state.displayName=name||'Visitante';updateProfile();persist();});
  $('#model-select').addEventListener('click',()=>{const pop=$('#model-popover');pop.hidden=!pop.hidden;if(!pop.hidden){$('#generation-list').hidden=false;$('#model-submenu').hidden=true;}$('#model-select').setAttribute('aria-expanded',String(!pop.hidden));});
  function updateModelMenu() {
    $('#model-select-label').textContent = state.generation + ' · ' + state.model;
    $$('[data-generation]').forEach(button=>button.classList.toggle('selected',button.dataset.generation===state.generation));
    $$('[data-model]').forEach(button=>button.classList.toggle('selected',button.dataset.model===state.model));
  }
  $$('[data-generation]').forEach(button=>button.addEventListener('click',()=>{if(button.disabled)return;state.generation=button.dataset.generation;updateModelMenu();$('#generation-list').hidden=true;$('#model-submenu').hidden=false;persist();}));
  $('#generation-back').addEventListener('click',()=>{$('#model-submenu').hidden=true;$('#generation-list').hidden=false;});
  $$('[data-model]').forEach(button=>button.addEventListener('click',()=>{state.model=button.dataset.model;updateModelMenu();persist();$('#model-popover').hidden=true;$('#model-select').setAttribute('aria-expanded','false');toast('Perfil de exibição atualizado.');}));
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
  hydrateIcons(); restore(); applyTheme(); renderHistory(); renderConversation(); updateProfile(); updateModelMenu(); refreshAuthSession();
})();