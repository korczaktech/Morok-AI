(() => {
  'use strict';
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const STORAGE_KEY = 'morok.web.state.v2';
  const AUTH_API_URL = 'https://morok-ai.onrender.com';
  const MAX_LOCAL_BYTES = 3_500_000;
  const state = { conversations: [], activeId: null, theme: 'dark', apiUrl: AUTH_API_URL, authSession: null, authMode: 'login', generation: 'G1', model: 'Tsukuyomi', displayName: 'Visitante', attachments: [], busy: false, controller: null, contextId: null, renameId: null, toastTimer: null };
  const el = {
    app: $('#app'), sidebar: $('#sidebar'), backdrop: $('#backdrop'), history: $('#history-list'),
    welcome: $('#welcome'), conversation: $('#conversation'), input: $('#message-input'), composer: $('#composer'),
    send: $('#send-button'), tray: $('#attachment-tray'), file: $('#file-input'), status: $('#status-label'),
    dot: $('#status-dot'), pill: $('#connection-pill'), settings: $('#settings-dialog'), rename: $('#rename-dialog'),
    profile: $('#profile-dialog'), auth: $('#auth-dialog'), context: $('#context-menu'), toast: $('#toast')
  };

  function id() { return (crypto.randomUUID ? crypto.randomUUID() : Date.now().toString(36) + Math.random().toString(36).slice(2)); }
  function now() { return new Date().toISOString(); }
  function formatTime(value) { try { return new Intl.DateTimeFormat('pt-BR', { hour: '2-digit', minute: '2-digit' }).format(new Date(value)); } catch { return ''; } }
  function titleFrom(text) { const clean = text.replace(/\s+/g, ' ').trim(); return clean.length > 34 ? clean.slice(0, 34).trimEnd() + '…' : clean || 'Nova conversa'; }
  function persist() {
    try {
      const payload = JSON.stringify({ conversations: state.conversations, activeId: state.activeId, theme: state.theme, apiUrl: state.apiUrl, displayName: state.displayName, generation: state.generation, model: state.model });
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
      const menu = document.createElement('button'); menu.type = 'button'; menu.className = 'history-menu'; menu.textContent = '···'; menu.title = 'Opções da conversa'; menu.setAttribute('aria-label', 'Opções de ' + conversation.title);
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
    const avatar = document.createElement('div'); avatar.className = 'message-avatar'; avatar.textContent = message.role === 'user' ? initials(state.displayName) : 'm';
    const content = document.createElement('div'); content.className = 'message-content';
    const meta = document.createElement('div'); meta.className = 'message-meta';
    const name = document.createElement('span'); name.className = 'message-name'; name.textContent = message.role === 'user' ? (state.displayName || 'Você') : 'Morok';
    const time = document.createElement('span'); time.className = 'message-time'; time.textContent = formatTime(message.createdAt);
    meta.append(name,time);
    const text = document.createElement('div'); text.className = 'message-text'; text.innerHTML = message.role === 'assistant' && !message.error ? renderMarkdown(message.content) : escapeHtml(message.content).replace(/\n/g,'<br>');
    content.append(meta,text);
    if (message.attachments?.length) {
      const files = document.createElement('div'); files.className = 'message-attachments';
      message.attachments.forEach(file => { const chip = document.createElement('div'); chip.className = 'message-file'; chip.textContent = '▧ ' + file.name; files.append(chip); });
      content.append(files);
    }
    if (message.role === 'assistant' && !message.error) {
      const actions = document.createElement('div'); actions.className = 'message-actions';
      const copy = document.createElement('button'); copy.className = 'message-action'; copy.textContent = 'Copiar'; copy.addEventListener('click', () => copyText(message.content));
      actions.append(copy);
      const conversation = activeConversation();
      if (conversation && conversation.messages.at(-1)?.id === message.id) {
        const retry = document.createElement('button'); retry.className = 'message-action'; retry.textContent = 'Gerar novamente'; retry.addEventListener('click', regenerateLast); actions.append(retry);
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
    $('#profile-subtitle').textContent = signedIn ? 'Conta autenticada' : 'Entre para usar sua conta';
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
  function renameConversation(conversationId) {
    const conversation = state.conversations.find(c => c.id === conversationId); if (!conversation) return;
    state.renameId = conversationId; $('#rename-input').value = conversation.title; closeContextMenu(); el.rename.showModal(); $('#rename-input').focus();
  }
  function deleteConversation(conversationId) {
    const conversation = state.conversations.find(c => c.id === conversationId); if (!conversation) return;
    if (!confirm('Excluir a conversa "' + conversation.title + '" deste dispositivo?')) return;
    state.conversations = state.conversations.filter(c => c.id !== conversationId);
    if (state.activeId === conversationId) state.activeId = state.conversations[0]?.id || null;
    persist(); renderHistory(); renderConversation(); toast('Conversa excluída.');
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
      const remove = document.createElement('button'); remove.type = 'button'; remove.textContent = '×'; remove.setAttribute('aria-label','Remover ' + file.name);
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
    el.send.classList.toggle('busy',busy); el.send.querySelector('span').textContent = busy ? '■' : '↑';
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
  function openSettings() {
    $('#api-url').value = state.apiUrl; $('#theme-select').value = state.theme; $('#test-result').textContent = ''; $('#auth-config-result').textContent = 'Autenticação conectada ao MongoDB Atlas.';
    el.settings.showModal();
  }
  function saveSettings() {
    const url = $('#api-url').value.trim();
    if (url) {
      try { const parsed = new URL(url); if (!['https:','http:'].includes(parsed.protocol)) throw new Error(); }
      catch { toast('Informe uma URL válida começando com https:// ou http://.'); return false; }
    }
    state.apiUrl = (url || AUTH_API_URL).replace(/\/+$/,'');
    state.theme = $('#theme-select').value;
    applyTheme(); persist(); el.settings.close(); checkHealth(); toast('Configurações salvas.'); return true;
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
      message.textContent=state.authMode==='signup'?'Conta criada e salva no MongoDB.':'Login realizado.';
      setTimeout(()=>el.auth.close(),500);
    } catch(error) { message.classList.add('error'); message.textContent=error.message; }
    finally { $('#auth-submit').disabled=false; }
  }
  async function logout() {
    try { if(state.authSession?.access_token) await authRequest('/auth/logout',{token:state.authSession.access_token}); } catch {}
    saveAuthSession(null); state.displayName='Visitante'; updateProfile(); persist(); openAuth('login'); $('#auth-message').className='auth-message success'; $('#auth-message').textContent='Sessão encerrada neste dispositivo.';
  }
  // Start API health check before optional UI bindings.
  restore(); applyTheme(); renderHistory(); renderConversation(); updateProfile(); updateModelMenu(); checkHealth(); refreshAuthSession();

  $$('.auth-tab').forEach(tab=>tab.addEventListener('click',()=>setAuthMode(tab.dataset.authMode)));
  $('#auth-form').addEventListener('submit',event=>{event.preventDefault();if(event.submitter && event.submitter.id!=='auth-submit')return;submitAuth();});
  $('#auth-logout').addEventListener('click',logout);

  $('#new-chat').addEventListener('click',createConversation);
  $('#brand-home').addEventListener('click',event=>{event.preventDefault();state.activeId=null;persist();renderHistory();renderConversation();closeSidebar();el.input.focus();});
  $('#all-chats').addEventListener('click',()=>{renderHistory();closeSidebar();});
  $('#search-open').addEventListener('click',()=>{const query=prompt('Buscar no título e no conteúdo das conversas:');if(query!==null){renderHistory(query);closeSidebar();}});
  $('#menu-toggle').addEventListener('click',openSidebar);
  $('#sidebar-close').addEventListener('click',closeSidebar);
  el.backdrop.addEventListener('click',closeSidebar);
  $('#settings-open').addEventListener('click',openSettings);
  $('#profile-button').addEventListener('click',()=>openAuth('login'));
  $('#profile-save').addEventListener('click',()=>{const name=$('#display-name').value.trim();state.displayName=name||'Visitante';updateProfile();persist();});
  $('#model-select').addEventListener('click',()=>{const pop=$('#model-popover');pop.hidden=!pop.hidden;if(!pop.hidden){$('#generation-list').hidden=false;$('#model-submenu').hidden=true;}$('#model-select').setAttribute('aria-expanded',String(!pop.hidden));});
  function updateModelMenu() {
    $('#model-select-label').textContent = state.generation + ' · ' + state.model;
    $$('[data-generation]').forEach(button=>button.classList.toggle('selected',button.dataset.generation===state.generation));
    $$('[data-model]').forEach(button=>button.classList.toggle('selected',button.dataset.model===state.model));
  }
  $$('[data-generation]').forEach(button=>button.addEventListener('click',()=>{if(button.disabled)return;state.generation=button.dataset.generation;updateModelMenu();$('#generation-list').hidden=true;$('#model-submenu').hidden=false;persist();}));
  $('#generation-back').addEventListener('click',()=>{$('#model-submenu').hidden=true;$('#generation-list').hidden=false;});
  $$('[data-model]').forEach(button=>button.addEventListener('click',()=>{state.model=button.dataset.model;updateModelMenu();persist();$('#model-popover').hidden=true;$('#model-select').setAttribute('aria-expanded','false');toast('Perfil selecionado: '+state.generation+' · '+state.model+'. O servidor ainda usa o checkpoint carregado.');}));
  document.addEventListener('click',event=>{if(!event.target.closest('.model-select-wrap')){$('#model-popover').hidden=true;$('#model-select').setAttribute('aria-expanded','false');}if(!event.target.closest('.context-menu')&&!event.target.closest('.history-menu'))closeContextMenu();});
  el.context.addEventListener('click',event=>{const action=event.target.dataset.action;if(action==='rename')renameConversation(state.contextId);if(action==='delete')deleteConversation(state.contextId);closeContextMenu();});
  $('#rename-save').addEventListener('click',()=>{const conversation=state.conversations.find(c=>c.id===state.renameId);if(conversation){conversation.title=$('#rename-input').value.trim()||'Nova conversa';conversation.updatedAt=now();persist();renderHistory();}state.renameId=null;});
  $('#save-settings').addEventListener('click',event=>{event.preventDefault();saveSettings();});
  $('#auth-open').addEventListener('click',()=>{el.settings.close();openAuth('login');});
  $('#test-connection').addEventListener('click',async()=>{const url=$('#api-url').value.trim();state.apiUrl=(url || AUTH_API_URL).replace(/\/+$/,'');const ok=await checkHealth();$('#test-result').textContent=ok?'Servidor respondeu corretamente.':'Falha de conexão. Confira URL, HTTPS e CORS.';});
  $('#status-refresh').addEventListener('click',()=>checkHealth(true));
  $('#clear-data').addEventListener('click',()=>{if(!confirm('Apagar todas as conversas e preferências locais?'))return;state.conversations=[];state.activeId=null;state.displayName='Visitante';state.attachments=[];persist();renderHistory();renderConversation();updateProfile();renderAttachments();toast('Dados locais apagados.');});
  $('#attach-button').addEventListener('click',()=>el.file.click());
  el.file.addEventListener('change',event=>addFiles([...event.target.files]));
  $$('.suggestion').forEach(button=>button.addEventListener('click',()=>{el.input.value=button.dataset.prompt;resizeInput();el.input.focus();}));
  el.input.addEventListener('input',resizeInput);
  el.input.addEventListener('keydown',event=>{if(event.key==='Enter'&&!event.shiftKey){event.preventDefault();if(!state.busy)sendMessage();else state.controller?.abort();}});
  el.composer.addEventListener('submit',event=>{event.preventDefault();sendMessage();});
  document.addEventListener('keydown',event=>{
    if(event.key==='Escape'){closeSidebar();closeContextMenu();$('#model-popover').hidden=true;}
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='k'){event.preventDefault();el.input.focus();}
    if((event.ctrlKey||event.metaKey)&&event.shiftKey&&event.key.toLowerCase()==='o'){event.preventDefault();createConversation();}
  });
  matchMedia('(prefers-color-scheme: light)').addEventListener?.('change',()=>{if(state.theme==='system')applyTheme();});
  restore(); applyTheme(); renderHistory(); renderConversation(); updateProfile(); updateModelMenu(); refreshAuthSession();
})();