// SPDX-License-Identifier: GPL-3.0-or-later
// MihawkOS's WebUI, for players: their games and each one's settings, a game copied from this computer and set up in
// three steps, home screen tiles, save backups, the launcher's settings, and help. The routes are src/pw_webui.hpp's;
// every write carries the token the launcher made at its start (docs/WEBUI.md).
'use strict';

const $ = (selector, root = document) => root.querySelector(selector);
const el = (tag, props = {}, ...children) => {
  const node = Object.assign(document.createElement(tag), props);
  for (const child of children) node.append(child);
  return node;
};
let token = '';

// The page's words: in the language chosen on the Help page, else the console's (its guided setup's), else English.
// webui/i18n.js holds each language's text keyed by the English; {0}, {1} are the values put in.
let words = {};
const t = (text, ...values) => (words[text] || text).replace(/\{(\d)\}/g, (_, i) => values[i]);
const storedLanguage = () => { try { return localStorage.getItem('mihawk-language') || ''; } catch (error) { return ''; } };
function useLanguage(tag) {
  const known = ((window.MIHAWK_I18N || {}).languages || []).some((language) => language.tag === tag);
  const chosen = known ? tag : 'en-US';
  words = ((window.MIHAWK_I18N || {}).text || {})[chosen] || {};
  document.documentElement.lang = chosen;
  // The page's own text: each text node and placeholder by its English, kept on the node for the next switch
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    if (node.parentElement.closest('script, style, pre, .dynamic')) continue;
    if (node.english === undefined) node.english = node.textContent;
    const key = node.english.trim().replace(/\s+/g, ' ');
    if (key) node.textContent = node.english.replace(node.english.trim(), t(key));
  }
  for (const input of document.querySelectorAll('[placeholder], [aria-label]')) {
    for (const name of ['placeholder', 'aria-label']) {
      if (!input.hasAttribute(name)) continue;
      const kept = `english${name === 'placeholder' ? 'Placeholder' : 'Label'}`;
      if (input.dataset[kept] === undefined) input.dataset[kept] = input.getAttribute(name);
      input.setAttribute(name, t(input.dataset[kept]));
    }
  }
  return chosen;
}

// ---- talking to the PS5 ----
function setOffline(offline) { $('#offline').hidden = !offline; }
async function refreshToken() {
  const status = await api('/api/status');
  token = status.token;
  return status;
}
async function api(path, options = {}) {
  const headers = Object.assign({}, options.headers || {});
  const writing = options.method && options.method !== 'GET';
  if (writing) headers['X-Mihawk-Token'] = token;
  let response;
  try {
    response = await fetch(path, Object.assign({}, options, { headers }));
  } catch (error) {
    setOffline(true);
    throw new Error(t('Your PS5 is not answering.'));
  }
  setOffline(false);
  const type = response.headers.get('Content-Type') || '';
  const body = type.startsWith('application/json') ? await response.json() : await response.text();
  // MihawkOS started again since this page opened (a game ended): its new token, and the write once more
  if (response.status === 403 && writing && !options.again) {
    await refreshToken();
    return api(path, Object.assign({}, options, { again: true }));
  }
  if (!response.ok) throw new Error(body && body.error ? t(body.error) : t('The PS5 answered with an error ({0}).', response.status));
  return body;
}
const post = (path, value) => api(path, { method: 'POST', body: JSON.stringify(value), headers: { 'Content-Type': 'application/json' } });

let toastTimer;
function toast(text, bad = false) {
  const node = $('#toast');
  node.textContent = text;
  node.classList.toggle('bad', bad);
  node.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { node.hidden = true; }, bad ? 6000 : 4000);
}

function bytes(n) {
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  while (n >= 1000 && i < units.length - 1) { n /= 1000; i++; }
  return `${n >= 10 || i === 0 ? Math.round(n) : n.toFixed(1)} ${units[i]}`;
}
function duration(seconds) {
  if (!isFinite(seconds) || seconds < 0) return '…';
  if (seconds < 60) return t('{0} s', Math.max(1, Math.round(seconds)));
  if (seconds < 3600) return t('{0} min', Math.round(seconds / 60));
  return t('{0} h {1} min', Math.floor(seconds / 3600), Math.round((seconds % 3600) / 60));
}

// ---- pages ----
const loaders = { games: loadGames, add: () => {}, tiles: loadTiles, saves: loadSaves, settings: loadSettings, help: loadHelp };
function show() {
  const tab = (location.hash || '#games').slice(1);
  const page = loaders[tab] ? tab : 'games';
  for (const section of document.querySelectorAll('.page')) section.hidden = section.id !== `page-${page}`;
  window.scrollTo(0, 0);
  for (const link of document.querySelectorAll('#tabs a')) link.classList.toggle('active', link.dataset.tab === page);
  loaders[page]();
}

// The launcher's settings, also what a game's "Same as Settings" choices say
let settings = null;
async function settingsNow() {
  if (!settings) settings = await api('/api/settings');
  return settings;
}

// ---- my games ----
let games = [];
const artOf = (game, kind = '') =>
  `/api/media/cover?id=${encodeURIComponent(game.id)}${kind ? `&kind=${kind}` : ''}&v=${game.cover}`;
function initial(name) {
  return el('span', { className: 'letter', textContent: (name || '?').trim().charAt(0).toUpperCase() });
}
async function loadGames() {
  const grid = $('#game-grid');
  try {
    games = (await api('/api/library')).games;
  } catch (error) {
    grid.replaceChildren(el('p', { className: 'error', textContent: error.message }));
    return;
  }
  grid.replaceChildren(...games.map((game) => {
    const art = el('div', { className: 'art' });
    art.append(game.cover ? el('img', { src: artOf(game), alt: '' }) : initial(game.name || game.id));
    const body = el('div', { className: 'body' }, el('div', { className: 'name dynamic', textContent: game.name || game.id }));
    const card = el('button', { className: 'glass game', type: 'button' }, art, body);
    if (game.id === 'mihawk') {
      body.append(el('div', { className: 'muted', textContent: t('The MihawkOS desktop, where your games have their shortcuts') }),
        el('span', { className: 'badge plain', textContent: t('Built in') }));
      card.disabled = true;
      return card;
    }
    const second = game.detail || (game.meta && game.meta.developers ? game.meta.developers.join(', ') : '');
    if (second) body.append(el('div', { className: 'muted dynamic', textContent: second }));
    if (game.complaint) {
      body.append(el('span', { className: 'badge problem', textContent: t('Needs attention') }),
        el('p', { className: 'complaint', textContent: t('It cannot start yet. Open it to fix it.') }),
        el('details', { className: 'why' }, el('summary', { textContent: t('Why?') }),
          el('span', { className: 'dynamic', textContent: game.complaint })));
    } else {
      body.append(el('span', { className: 'badge', textContent: t('Ready to play') }));
    }
    card.title = t('Change this game');
    card.addEventListener('click', (event) => { if (!event.target.closest('details')) openEditor(game); });
    return card;
  }));
  // Folders in C:\Games that no game starts a program from: one card each, to set it up
  const folders = await api('/api/files?prefix=mihawk&path=Games').catch(() => ({ dirs: [] }));
  const used = games.map((game) => (game.executable || '').toLowerCase());
  const loose = folders.dirs.filter((dir) => !used.some((exe) => exe.startsWith(`c:\\games\\${dir.toLowerCase()}\\`)));
  $('#loose').hidden = !loose.length;
  $('#loose-grid').replaceChildren(...loose.map((dir) => {
    const card = el('button', { className: 'glass game loose', type: 'button' }, el('div', { className: 'art' }, initial(dir)),
      el('div', { className: 'body' }, el('div', { className: 'name dynamic', textContent: dir }),
        el('div', { className: 'muted', textContent: `C:\\Games\\${dir}` }), el('span', { className: 'badge action', textContent: t('Set it up') })));
    card.addEventListener('click', () => openEditor(null, { name: dir, folder: dir }));
    return card;
  }));
  $('#welcome').hidden = games.some((game) => game.id !== 'mihawk') || loose.length > 0;
}

function slug(name) {
  return name.normalize('NFKD').replace(/[^A-Za-z0-9]+/g, '-').replace(/^-+|-+$/g, '').toLowerCase().slice(0, 64) || 'game';
}

// ---- a game's settings ----
// game.json "env" words the editor shows as choices of its own (src/pw_game_mode.h, src/pw_controller.h)
const GAME_MODE = 'WINE_PS5_GAME_MODE', CONTROLLER = 'WINE_PS5_CONTROLLER', OPENGL = 'WINE_PS5_OPENGL=1';
const word = (env, name) => { const found = env.filter((w) => w.startsWith(`${name}=`)).pop(); return found ? found.slice(name.length + 1) : ''; };
let editing = null, fromWizard = false;
async function openEditor(game, preset = {}) {
  editing = game || null;
  fromWizard = !!preset.wizard;
  const form = $('#editor-form');
  form.reset();
  $('#editor-error').textContent = '';
  $('#editor-title').textContent = game ? (game.name || game.id) : t('Set up a new game');
  const env = (game && game.env) || [];
  form.name.value = game ? game.name : (preset.name || '');
  form.detail.value = game ? game.detail : '';
  form.executable.value = game ? game.executable : (preset.executable || '');
  form.arguments.value = game ? game.arguments : '';
  form.directory.value = game ? game.directory : '';
  form.prefix.value = game ? game.prefix : '';
  form.id.value = game ? game.id : '';
  form.id.readOnly = !!game;
  form.desktop.checked = game ? game.desktop : true;
  form.launcher.checked = game ? game.launcher : true;   // a game set up here shows in the launcher's list
  form.opengl.checked = env.includes(OPENGL);
  form.env.value = env.filter((w) => w !== OPENGL && !w.startsWith(`${GAME_MODE}=`) && !w.startsWith(`${CONTROLLER}=`)).join('\n');
  pickerStart = preset.folder ? `Games/${preset.folder}` : '';
  showProgram();
  $('#editor-delete-row').hidden = !game;
  try {
    const now = await settingsNow();
    const same = (label) => t('Same as Settings ({0})', label);
    form.game_mode.replaceChildren(el('option', { value: '', textContent: same(now.game_mode ? t('On') : t('Off')) }),
      el('option', { value: '1', textContent: t('On') }), el('option', { value: '0', textContent: t('Off') }));
    const current = now.choices.controllers.find((choice) => choice.id === now.controller);
    form.controller.replaceChildren(el('option', { value: '', textContent: same(current ? t(current.label) : '') }),
      ...now.choices.controllers.map((choice) => el('option', { value: choice.id, textContent: t(choice.label) })));
  } catch (error) { /* the choices stay empty: the toast said why */ }
  form.game_mode.value = ['0', '1'].includes(word(env, GAME_MODE)) ? word(env, GAME_MODE) : '';
  form.controller.value = word(env, CONTROLLER);
  if (form.controller.selectedIndex < 0) form.controller.value = '';
  showMedia(game);
  $('#editor').showModal();
  // A new game: its program guessed, its picture looked up, so a player only has to confirm
  if (!game && preset.folder && !form.executable.value) suggestProgram(`Games/${preset.folder}`);
  if (!game && form.name.value) findArt();
}

function showProgram() {
  const path = $('#editor-form').executable.value.trim();
  $('#program-path').textContent = path || t('No program chosen yet');
  $('#program-path').classList.toggle('empty', !path);
}

function entryFromForm() {
  const form = $('#editor-form');
  if (!form.id.value) form.id.value = slug(form.name.value);
  const env = form.env.value.split('\n').map((w) => w.trim()).filter(Boolean)
    .filter((w) => !w.startsWith(`${GAME_MODE}=`) && !w.startsWith(`${CONTROLLER}=`) && w !== OPENGL);
  if (form.opengl.checked) env.push(OPENGL);
  if (form.game_mode.value) env.push(`${GAME_MODE}=${form.game_mode.value}`);
  if (form.controller.value) env.push(`${CONTROLLER}=${form.controller.value}`);
  return {
    id: form.id.value, name: form.name.value.trim(), detail: form.detail.value.trim(), executable: form.executable.value.trim(),
    arguments: form.arguments.value.trim(), directory: form.directory.value.trim(), prefix: form.prefix.value.trim(),
    desktop: form.desktop.checked, launcher: form.launcher.checked, env,
  };
}

// Saves the entry; a new one becomes the one being edited. False, with the reason shown, when it could not.
async function saveEntry() {
  const entry = entryFromForm();
  if (!entry.name) { $('#editor-error').textContent = t('Give the game a name.'); return false; }
  if (!entry.executable) { $('#editor-error').textContent = t('Choose the program that starts the game.'); return false; }
  if (!editing && games.some((game) => game.id === entry.id)) {
    $('#editor-error').textContent = t('Another game already uses the id {0}. Change it under Advanced settings.', entry.id);
    return false;
  }
  try {
    await post('/api/library/game', entry);
  } catch (error) {
    $('#editor-error').textContent = error.message;
    return false;
  }
  await loadGames();
  editing = games.find((game) => game.id === entry.id) || entry;
  $('#editor-delete-row').hidden = false;
  return true;
}

async function saveEditor(event) {
  event.preventDefault();
  const wasNew = !editing;
  if (!await saveEntry()) return;
  $('#editor').close();
  toast(t('{0} is saved. MihawkOS shows it now.', editing.name));
  if (wasNew && fromWizard) wizardDone(editing);
}

async function deleteEntry() {
  if (!editing || !confirm(t('Remove {0} from MihawkOS? The game\'s files stay on your PS5, in C:\\Games.', editing.name))) return;
  try {
    await post('/api/library/delete', { id: editing.id });
    $('#editor').close();
    toast(t('{0} is removed from MihawkOS.', editing.name));
    loadGames();
  } catch (error) {
    $('#editor-error').textContent = error.message;
  }
}

// ---- the program picker: the game's folder, its .exe files, the likeliest one suggested ----
let pickerPath = 'Games', pickerStart = '';
const NOT_THE_GAME = /unins|setup|install|crash|redist|vc_?redist|dxsetup|dotnet|prereq|report|helper|updater/i;
function suggestion(exes, folder) {
  const candidates = exes.filter((exe) => !NOT_THE_GAME.test(exe.name));
  const plain = (text) => text.toLowerCase().replace(/\.exe$/, '').replace(/[^a-z0-9]/g, '');
  const named = candidates.find((exe) => plain(folder).includes(plain(exe.name)) || plain(exe.name).includes(plain(folder)));
  return named || candidates.sort((a, b) => b.size - a.size)[0] || null;
}
async function suggestProgram(path) {
  try {
    const listing = await api(`/api/browse?prefix=mihawk&path=${encodeURIComponent(path)}`);
    const best = suggestion(listing.exes, path.split('/').pop());
    if (best && !$('#editor-form').executable.value) {
      $('#editor-form').executable.value = `${listing.windows}\\${best.name}`;
      showProgram();
    }
  } catch (error) { /* the player chooses it */ }
}
async function browse(path) {
  const form = $('#editor-form');
  const prefix = form.prefix.value.trim() || 'mihawk';
  let listing;
  try {
    listing = await api(`/api/browse?prefix=${encodeURIComponent(prefix)}&path=${encodeURIComponent(path)}`);
  } catch (error) {
    if (path !== 'Games') return browse('Games');
    $('#picker-path').textContent = error.message;
    return;
  }
  pickerPath = listing.path;
  $('#picker-path').textContent = listing.windows;
  $('#picker-up').disabled = pickerPath === 'Games';
  const best = suggestion(listing.exes, pickerPath.split('/').pop());
  const rows = [];
  for (const exe of listing.exes) {
    const row = el('button', { type: 'button', className: `exe${exe === best ? ' best' : ''}` },
      el('span', { className: 'kind', textContent: t('Program') }), el('span', { className: 'grow dynamic', textContent: exe.name }),
      exe === best ? el('span', { className: 'badge', textContent: t('Suggested') }) : '', el('span', { className: 'size', textContent: bytes(exe.size) }));
    row.addEventListener('click', () => {
      form.executable.value = `${listing.windows}\\${exe.name}`;
      if (!form.name.value) form.name.value = exe.name.replace(/\.exe$/i, '');
      showProgram();
      $('#picker').close();
    });
    rows.push(row);
  }
  for (const dir of listing.dirs) {
    const row = el('button', { type: 'button', className: 'dir' }, el('span', { className: 'kind', textContent: t('Folder') }),
      el('span', { className: 'grow dynamic', textContent: dir }));
    row.addEventListener('click', () => browse(`${pickerPath}/${dir}`));
    rows.push(row);
  }
  if (!rows.length) rows.push(el('p', { className: 'muted', textContent: t('This folder holds no program. Go to the folder above.') }));
  $('#picker-list').replaceChildren(...rows);
}
function openPicker() {
  const form = $('#editor-form');
  const exe = form.executable.value.trim().replace(/\//g, '\\');
  let start = pickerStart || 'Games';
  if (/^c:\\games\\/i.test(exe)) start = exe.slice(3, exe.lastIndexOf('\\')).replace(/\\/g, '/');
  $('#picker').showModal();
  browse(start);
}

// ---- picture and details: the store's matches, the one chosen applied by the PS5; or the player's own picture ----
function showMedia(game) {
  const cover = $('#media-cover');
  $('#media-matches').replaceChildren();
  $('#media-name').value = game ? game.name : $('#editor-form').name.value;
  cover.hidden = !(game && game.cover);
  if (game && game.cover) cover.src = artOf(game);
  const meta = game && game.meta;
  $('#media-meta').textContent = meta
    ? [meta.name, meta.release, (meta.developers || []).join(', '), (meta.genres || []).join(', ')].filter(Boolean).join(' · ') +
      (meta.description ? `\n${meta.description}` : '')
    : t('No picture yet. Look the game up in the Steam store, or use a picture of your own.');
  $('#media-remove').hidden = !(game && (game.cover || meta));
}
async function refreshEditing() {
  await loadGames();
  const fresh = games.find((game) => editing && game.id === editing.id);
  if (fresh) { editing = fresh; showMedia(fresh); }
}
async function findArt() {
  const name = $('#media-name').value.trim() || $('#editor-form').name.value.trim();
  if (!name) return;
  $('#media-name').value = name;
  $('#media-matches').replaceChildren(el('p', { className: 'muted', textContent: t('Looking it up…') }));
  try {
    const { matches } = await post('/api/media/search', { name });
    $('#media-matches').replaceChildren(...(matches.length ? [el('p', { className: 'hint wide-row', textContent: t('Choose the right game to use its picture and details:') }),
      ...matches.map((match) => {
        const image = el('img', { src: `/api/media/thumb?appid=${match.appid}`, alt: '' });
        image.onerror = () => image.replaceWith(el('div', { className: 'no-image' }, initial(match.name)));   // the store has none
        const button = el('button', { type: 'button', title: match.name }, image, el('span', { className: 'dynamic', textContent: match.name }));
        button.addEventListener('click', () => applyArt(match));
        return button;
      })] : [el('p', { className: 'muted wide-row', textContent: t('Nothing found. Try a shorter name, or use a picture of your own.') })]));
  } catch (error) {
    $('#media-matches').replaceChildren(el('p', { className: 'error wide-row', textContent: error.message }));
  }
}
async function applyArt(match) {
  if (!editing && !await saveEntry()) return;   // a new game is saved first: its picture belongs to it
  $('#media-matches').replaceChildren(el('p', { className: 'muted wide-row', textContent: t('Getting the picture of {0}…', match.name) }));
  try {
    await post('/api/media/apply', { id: editing.id, appid: match.appid });
    toast(t('The picture and details of {0} are saved.', match.name));
    $('#media-matches').replaceChildren();
    refreshEditing();
  } catch (error) {
    $('#media-matches').replaceChildren(el('p', { className: 'error wide-row', textContent: error.message }));
  }
}
async function ownPicture(file) {
  $('#media-own').value = '';
  if (!file || (!editing && !await saveEntry())) return;
  try {
    await api(`/api/media/cover?id=${encodeURIComponent(editing.id)}&kind=header`, { method: 'PUT', body: file });
    toast(t('Your picture is saved.'));
    refreshEditing();
  } catch (error) {
    toast(error.message, true);
  }
}
async function removeArt() {
  if (!editing) return;
  try {
    await post('/api/media/remove', { id: editing.id });
    toast(t('The picture and details are removed.'));
    refreshEditing();
  } catch (error) {
    toast(error.message, true);
  }
}

// ---- add a game: a folder chosen, copied 8 MiB at a time (each file resumed where the PS5's copy ends), set up ----
const piece = 8 << 20;
let chosen = [], sending = false, stopRequested = false, copiedFolder = '';
function wizardStep(step) {
  for (let i = 1; i <= 3; ++i) $(`#add-step-${i}`).hidden = i !== step;
  for (const item of document.querySelectorAll('#stepper li'))
    item.className = Number(item.dataset.step) < step ? 'done' : Number(item.dataset.step) === step ? 'on' : '';
}
function chooseFiles(list) {
  chosen = Array.from(list).filter((file) => file.webkitRelativePath || file.name);
  $('#folder-input').value = '';
  if (!chosen.length) return;
  const top = (chosen[0].webkitRelativePath || chosen[0].name).split('/')[0];
  $('#upload-name').value = chosen[0].webkitRelativePath ? top.replace(/[\\:]/g, '_').replace(/^\.+/, '') : t('New game');
  const total = chosen.reduce((sum, file) => sum + file.size, 0);
  $('#upload-summary').textContent = t('Files: {0} · {1} in all', chosen.length, bytes(total));
  $('#upload-bar').style.width = '0%';
  $('#upload-status').textContent = '';
  wizardStep(2);
}
function relativeName(file) {
  const parts = (file.webkitRelativePath || file.name).split('/');
  if (file.webkitRelativePath) parts.shift();   // the chosen folder's own name: the PS5's folder replaces it
  return parts.join('/');
}

// Files to the PS5, each under target(file) (a path under drive_c), 8 MiB at a time, each resumed where the PS5's copy
// ends; progress(done, total). Throws on the first refusal. Returns how many hidden files it left out.
async function sendFiles(prefix, files, target, progress) {
  const total = files.reduce((sum, file) => sum + file.size, 0);
  let done = 0, skipped = 0;
  for (const file of files) {
    if (stopRequested) throw new Error(t('Stopped. Choose the same folder again to carry on.'));
    const path = target(file);
    if (!path || path.split('/').some((part) => !part || part.startsWith('.'))) { skipped++; done += file.size; continue; }
    const query = `prefix=${encodeURIComponent(prefix)}&path=${encodeURIComponent(path)}`;
    const state = await api(`/api/upload/status?${query}`);
    let offset = state.done && state.have === file.size ? file.size : (state.done ? 0 : state.have);
    done += offset;
    while (offset < file.size || (file.size === 0 && !state.done)) {
      const end = Math.min(file.size, offset + piece);
      await api(`/api/upload?${query}&offset=${offset}&size=${file.size}`, { method: 'PUT', body: file.slice(offset, end) });
      done += end - offset;
      offset = end;
      progress(done, total);
      if (file.size === 0) break;
    }
  }
  progress(total, total);
  return skipped;
}

async function sendFolder() {
  const folder = $('#upload-name').value.trim();
  if (!folder || /^[.]|[\\/:]/.test(folder)) {
    toast(t('The folder name cannot start with a dot, or hold \\ / or :.'), true);
    return;
  }
  sending = true; stopRequested = false;
  $('#upload-start').disabled = true;
  $('#upload-cancel').textContent = t('Stop');
  const started = performance.now();
  let first = -1;
  try {
    const skipped = await sendFiles('mihawk', chosen, (file) => `Games/${folder}/${relativeName(file)}`, (done, all) => {
      if (first < 0) first = done;   // what an earlier copy had already sent does not count toward the speed
      const seconds = (performance.now() - started) / 1000, speed = (done - first) / Math.max(seconds, 0.1);
      $('#upload-bar').style.width = `${Math.min(100, (100 * done) / Math.max(all, 1))}%`;
      $('#upload-status').textContent = t('{0} of {1} copied · {2}/s · about {3} left', bytes(done), bytes(all), bytes(speed),
        duration(speed > 0 ? (all - done) / speed : Infinity));
    });
    copiedFolder = folder;
    $('#upload-status').textContent = t('Copied: {0}.', bytes(chosen.reduce((sum, file) => sum + file.size, 0)));
    $('#added-title').textContent = t('3. Set it up');
    $('#added-text').textContent = t('{0} is on your PS5.', folder) + ' ' +
      (skipped ? t('Hidden files left out: {0}.', skipped) + ' ' : '') + t('Now choose the program that starts it, and its picture.');
    $('#added-setup').hidden = false;
    $('#added-tile').hidden = true;
    wizardStep(3);
    setUpCopied();
  } catch (error) {
    $('#upload-status').textContent = error.message;
    toast(error.message, true);
  } finally {
    sending = false;
    $('#upload-start').disabled = false;
    $('#upload-cancel').textContent = t('Cancel');
  }
}
function setUpCopied() { openEditor(null, { name: copiedFolder, folder: copiedFolder, wizard: true }); }
function wizardDone(game) {
  $('#added-title').textContent = t('{0} is ready', game.name);
  $('#added-text').textContent = game.launcher
    ? t('Start it from MihawkOS\'s game list on your PS5. You can also put it on your PS5\'s home screen.')
    : game.desktop ? t('Start it from its shortcut on the Mihawk OS desktop. You can also put it on your PS5\'s home screen.')
    : t('You can put it on your PS5\'s home screen, or show it in MihawkOS\'s game list on My games.');
  $('#added-setup').hidden = true;
  $('#added-tile').hidden = false;
  $('#added-tile').dataset.game = game.id;
  wizardStep(3);
  $('#stepper li[data-step="3"]').className = 'done';
}

// ---- home screen tiles: one game each, made by the PS5, added through ShadowMountPlus ----
const iconImage = (src) => new Promise((resolve) => {
  const image = new Image();
  image.onload = () => resolve(image);
  image.onerror = () => resolve(null);
  image.src = src;
});
// The tile's icon: one of the game's pictures (its tall cover or its wide banner) in the square as the player chooses
// it (filling it, fitted whole over a blurred copy, stretched, or at its own size), moved and zoomed, the MihawkOS mark
// in a corner. The pictures are loaded once per game; a drag, the zoom or a choice only repaints.
let tileArts = {}, tileMark = null, tileView = { zoom: 1, x: 0, y: 0, fit: 'fill', art: 'cover' };
const tileArt = () => tileArts[tileView.art] || tileArts.cover || tileArts.banner || null;
async function loadTileArt() {
  const game = games.find((entry) => entry.id === $('#tile-game').value);
  tileArts = {};
  if (game && game.cover) {
    const [cover, banner] = await Promise.all([iconImage(artOf(game, 'portrait')), iconImage(artOf(game))]);
    if (cover) tileArts.cover = cover;
    if (banner) tileArts.banner = banner;
  }
  if (!tileMark) tileMark = await iconImage('/icon.png');
  tileView = { zoom: 1, x: 0, y: 0, fit: 'fill', art: tileArts.cover ? 'cover' : 'banner' };
  $('#tile-zoom').value = '1';
  $('#tile-adjust').hidden = !tileArt();
  $('#tile-art-choice').hidden = !(tileArts.cover && tileArts.banner);
  markTileChoices();
  paintTile();
}
function markTileChoices() {
  for (const button of document.querySelectorAll('#tile-fit button')) button.classList.toggle('on', button.dataset.fit === tileView.fit);
  for (const button of document.querySelectorAll('#tile-art button')) button.classList.toggle('on', button.dataset.art === tileView.art);
  $('#tile-zoom-row').hidden = tileView.fit === 'stretch';
  $('#tile-zoom').min = tileView.fit === 'fill' ? '1' : '0.5';   // filled, the picture always covers the square
}
function paintTile() {
  const g = $('#tile-icon').getContext('2d');
  const game = games.find((entry) => entry.id === $('#tile-game').value);
  const art = tileArt();
  const sky = g.createLinearGradient(0, 0, 0, 512);
  sky.addColorStop(0, '#5aa9ea'); sky.addColorStop(0.6, '#94d1fb'); sky.addColorStop(1, '#5cb32b');
  g.fillStyle = sky;
  g.fillRect(0, 0, 512, 512);
  if (art && tileView.fit === 'stretch') {
    g.drawImage(art, 0, 0, 512, 512);
  } else if (art) {
    const cover = Math.max(512 / art.width, 512 / art.height), whole = Math.min(512 / art.width, 512 / art.height);
    if (tileView.fit !== 'fill') {   // behind a picture that does not cover the square: itself, blurred and darker
      g.save();
      g.filter = 'blur(18px) brightness(0.75)';
      g.drawImage(art, (512 - art.width * cover) / 2, (512 - art.height * cover) / 2, art.width * cover, art.height * cover);
      g.restore();
      g.fillStyle = 'rgba(0, 0, 0, 0.18)';
      g.fillRect(0, 0, 512, 512);
    }
    const scale = (tileView.fit === 'fill' ? cover : tileView.fit === 'fit' ? whole : 1) * tileView.zoom;
    const width = art.width * scale, height = art.height * scale;
    // it moves as far as it overflows the square, or as far as the square has room for it
    const roomX = Math.abs(width - 512) / 2, roomY = Math.abs(height - 512) / 2;
    tileView.x = Math.max(-roomX, Math.min(roomX, tileView.x));
    tileView.y = Math.max(-roomY, Math.min(roomY, tileView.y));
    g.drawImage(art, (512 - width) / 2 + tileView.x, (512 - height) / 2 + tileView.y, width, height);
  } else {
    g.fillStyle = '#fff';
    g.font = '600 220px Inter, sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.fillText(((game && game.name) || '?').trim().charAt(0).toUpperCase(), 256, 270);
  }
  g.fillStyle = 'rgba(255,255,255,0.92)';
  g.beginPath();
  g.roundRect(392, 392, 104, 104, 22);
  g.fill();
  if (tileMark) g.drawImage(tileMark, 400, 400, 88, 88);
}
function tileControls() {
  const canvas = $('#tile-icon');
  let from = null;
  canvas.addEventListener('pointerdown', (event) => {
    if (!tileArt() || tileView.fit === 'stretch') return;
    from = { x: event.clientX, y: event.clientY };
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener('pointermove', (event) => {
    if (!from) return;
    const ratio = 512 / canvas.clientWidth;   // the preview is drawn smaller than the icon
    tileView.x += (event.clientX - from.x) * ratio;
    tileView.y += (event.clientY - from.y) * ratio;
    from = { x: event.clientX, y: event.clientY };
    paintTile();
  });
  for (const end of ['pointerup', 'pointercancel']) canvas.addEventListener(end, () => { from = null; });
  $('#tile-zoom').addEventListener('input', (event) => { tileView.zoom = Number(event.target.value); paintTile(); });
  $('#tile-reset').addEventListener('click', () => {
    Object.assign(tileView, { zoom: 1, x: 0, y: 0 });
    $('#tile-zoom').value = '1';
    paintTile();
  });
  for (const button of document.querySelectorAll('#tile-fit button'))
    button.addEventListener('click', () => { Object.assign(tileView, { fit: button.dataset.fit, zoom: 1, x: 0, y: 0 }); $('#tile-zoom').value = '1'; markTileChoices(); paintTile(); });
  for (const button of document.querySelectorAll('#tile-art button'))
    button.addEventListener('click', () => { Object.assign(tileView, { art: button.dataset.art, x: 0, y: 0 }); markTileChoices(); paintTile(); });
}
async function loadTiles() {
  try { games = (await api('/api/library')).games; } catch (error) { /* shown below */ }
  const select = $('#tile-game');
  const wanted = $('#added-tile').dataset.game || select.value;
  delete $('#added-tile').dataset.game;
  select.replaceChildren(...games.filter((game) => !game.complaint)
    .map((game) => el('option', { value: game.id, textContent: game.name || game.id, selected: game.id === wanted })));
  $('#tile-name').value = select.selectedOptions[0] ? select.selectedOptions[0].textContent : '';
  loadTileArt();
  let state;
  try {
    state = await api('/api/forwarders');
  } catch (error) {
    $('#tile-list').replaceChildren(el('p', { className: 'error', textContent: error.message }));
    return;
  }
  const ready = state.shadowmount.answers;
  $('#smp-dot').className = `status-dot ${ready ? 'ok' : 'bad'}`;
  $('#smp-title').textContent = ready ? t('Ready to add tiles') : t('ShadowMountPlus is not running');
  $('#smp-text').textContent = ready
    ? t('ShadowMountPlus is running on your PS5. It is the app that puts the tiles on the home screen.')
    : t('ShadowMountPlus, a separate app, puts the tiles on the home screen. Start it on your PS5, then press Check again.');
  $('#tile-make').disabled = !ready;
  $('#tile-error').textContent = '';
  $('#tile-list').replaceChildren(...(state.tiles.length ? state.tiles.map((tile) => {
    const game = games.find((entry) => entry.id === tile.game);
    const remove = el('button', { type: 'button', className: 'button small', textContent: tile.pending ? t('Being removed') : t('Remove'), disabled: tile.pending });
    remove.addEventListener('click', () => deleteTile(tile));
    const note = tile.pending ? t('It leaves the home screen a few seconds after you close MihawkOS.')
      : tile.adding ? t('It appears on the home screen a few seconds after you close MihawkOS.')
      : tile.installed ? t('On the home screen') : t('Not on the home screen yet');
    return el('div', { className: 'tile-row' }, el('img', { src: `/api/forwarders/icon?id=${tile.id}`, alt: '' }),
      el('div', { className: 'grow' }, el('strong', { className: 'dynamic', textContent: tile.name }),
        el('div', { className: 'muted', textContent: `${t('Opens {0}', game ? game.name : tile.game)} · ${note}` })),
      remove);
  }) : [el('p', { className: 'muted', textContent: t('No tiles yet.') })]));
}
// The tile's icon as the PS5's own are: a 24-bit RGB PNG (no alpha), 512 x 512, each row Sub-filtered and the whole
// deflated by the browser (CompressionStream, zlib as PNG wants). The canvas's own PNG is 32-bit RGBA; the home
// screen drew such a tile without its picture (2026-10-26), where the system's icons and PS5_RetroArch's are RGB.
const crcTable = Array.from({ length: 256 }, (_, n) => { for (let k = 0; k < 8; k++) n = n & 1 ? 0xedb88320 ^ (n >>> 1) : n >>> 1; return n >>> 0; });
function crc32(bytes) {
  let c = 0xffffffff;
  for (const b of bytes) c = crcTable[(c ^ b) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
async function rgbPngBase64(canvas) {
  const { width, height } = canvas;
  const rgba = canvas.getContext('2d').getImageData(0, 0, width, height).data;
  const raw = new Uint8Array(height * (1 + width * 3));
  for (let y = 0, o = 0; y < height; y++) {
    raw[o++] = 1;   // Sub: each byte less the one of the pixel to its left
    for (let x = 0; x < width; x++, o += 3) {
      const i = (y * width + x) * 4;
      for (let c = 0; c < 3; c++) raw[o + c] = (rgba[i + c] - (x ? rgba[i - 4 + c] : 0)) & 255;
    }
  }
  const zipped = new Uint8Array(await new Response(new Blob([raw]).stream().pipeThrough(new CompressionStream('deflate'))).arrayBuffer());
  const parts = [Uint8Array.of(0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a)];
  const chunk = (type, data) => {
    const out = new Uint8Array(12 + data.length), view = new DataView(out.buffer);
    view.setUint32(0, data.length);
    out.set([...type].map((ch) => ch.charCodeAt(0)), 4);
    out.set(data, 8);
    view.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
    parts.push(out);
  };
  const header = new Uint8Array(13), head = new DataView(header.buffer);
  head.setUint32(0, width); head.setUint32(4, height); header[8] = 8; header[9] = 2;   // 8 bits, RGB
  chunk('IHDR', header);
  chunk('IDAT', zipped);
  chunk('IEND', new Uint8Array(0));
  const png = new Uint8Array(await new Blob(parts).arrayBuffer());
  let text = '';
  for (let i = 0; i < png.length; i += 0x8000) text += String.fromCharCode(...png.subarray(i, i + 0x8000));
  return btoa(text);
}
async function makeTile() {
  const name = $('#tile-name').value.trim();
  if (!name) { $('#tile-error').textContent = t('Give the tile a name.'); return; }
  $('#tile-make').disabled = true;
  try {
    const icon = await rgbPngBase64($('#tile-icon'));
    const made = await post('/api/forwarders', { game: $('#tile-game').value, name, icon });
    if (made.registered) toast(t('{0} appears on your home screen a few seconds after you close MihawkOS.', name));
    else tileFailed(t('ShadowMountPlus did not add the tile. Check that it is running on your PS5, then try again.'), made.why);
  } catch (error) {
    $('#tile-error').textContent = error.message;
  } finally {
    loadTiles();
  }
}
async function deleteTile(tile) {
  if (!confirm(t('Remove the tile {0} from your home screen?', tile.name))) return;
  try {
    const result = await post('/api/forwarders/delete', { id: tile.id });
    toast(result.state === 'removed' ? t('The tile {0} is removed.', tile.name)
      : result.worker ? t('The tile {0} leaves your home screen a few seconds after you close MihawkOS.', tile.name)
      : t('{0} is out of ShadowMountPlus. To finish, select the tile on the home screen, press OPTIONS and choose Delete.', tile.name));
    loadTiles();
  } catch (error) {
    await loadTiles();
    tileFailed(t('ShadowMountPlus did not remove the tile. Check that it is running on your PS5, then try again.'), error.message);
  }
}
// The page's words for a tile ShadowMountPlus refused; the PS5's own detail under them
function tileFailed(said, detail) {
  toast(said, true);
  $('#tile-error').textContent = detail ? `${said} (${detail})` : said;
}

// ---- saves: the common save folders backed up as a ZIP, a ZIP put back, the user folder browsed ----
let userRoot = 'users', savesPath = '';
const SAVE_PLACES = [
  ['Saved Games', () => t('Where many newer games save.')],
  ['Documents', () => t('Many games keep a My Games folder here.')],
  ['AppData/Roaming', () => t('Saves and settings of many games.')],
  ['AppData/LocalLow', () => t('Saves of games made with Unity.')],
  ['AppData/Local', () => t('Saves of many games, with their caches: it can be large.')],
];
const zipLink = (prefix, path) => `/api/zip?prefix=${encodeURIComponent(prefix)}&path=${encodeURIComponent(path)}`;
async function loadSaves() {
  const select = $('#saves-prefix');
  if (!select.options.length) {
    try {
      const { prefixes } = await api('/api/prefixes');
      select.replaceChildren(...prefixes.map((id) => el('option', { value: id, textContent: id === 'mihawk' ? t('Mihawk OS') : id, selected: id === 'mihawk' })));
    } catch (error) { toast(error.message, true); return; }
  }
  const prefix = select.value;
  const list = (path) => api(`/api/files?prefix=${encodeURIComponent(prefix)}&path=${encodeURIComponent(path)}`).catch(() => ({ dirs: [] }));
  const users = await list('users');
  const user = users.dirs.includes('Mihawk') ? 'Mihawk' : users.dirs.find((dir) => dir !== 'Public') || '';
  userRoot = user ? `users/${user}` : 'users';
  const [top, appdata] = user ? await Promise.all([list(userRoot), list(`${userRoot}/AppData`)]) : [{ dirs: [] }, { dirs: [] }];
  const there = (place) => (place.startsWith('AppData/') ? appdata.dirs.includes(place.slice(8)) : top.dirs.includes(place));
  const places = SAVE_PLACES.filter(([place]) => there(place));
  $('#save-places').replaceChildren(...(places.length ? places.map(([place, about]) =>
    el('div', { className: 'place' }, el('div', { className: 'grow' }, el('strong', { className: 'dynamic', textContent: place.replace('/', ' › ') }),
      el('div', { className: 'muted', textContent: about() })),
    el('a', { className: 'button small', href: zipLink(prefix, `${userRoot}/${place}`), download: '', textContent: t('Download (.zip)') })))
    : [el('p', { className: 'muted', textContent: t('No save folders yet: start a game once, and its saves appear here.') })]));
  if (!savesPath.startsWith('users')) savesPath = userRoot;
  if ($('#saves-browser').open) browseSaves(savesPath);
}
async function browseSaves(path) {
  const prefix = $('#saves-prefix').value;
  let listing;
  try {
    listing = await api(`/api/files?prefix=${encodeURIComponent(prefix)}&path=${encodeURIComponent(path)}`);
  } catch (error) {
    if (path !== 'users') return browseSaves('users');
    $('#saves-list').replaceChildren(el('p', { className: 'error', textContent: error.message }));
    return;
  }
  savesPath = listing.path;
  $('#saves-path').textContent = listing.windows;
  $('#saves-up').disabled = savesPath === 'users';
  const zip = $('#saves-zip');
  zip.hidden = savesPath === 'users';
  zip.href = zipLink(prefix, savesPath);
  const rows = listing.dirs.map((dir) => {
    const row = el('button', { type: 'button', className: 'dir' }, el('span', { className: 'kind', textContent: t('Folder') }),
      el('span', { className: 'grow dynamic', textContent: dir }));
    row.addEventListener('click', () => browseSaves(`${savesPath}/${dir}`));
    return row;
  });
  for (const file of listing.files) {
    rows.push(el('a', { className: 'exe button-row', href: `/api/file?prefix=${encodeURIComponent(prefix)}&path=${encodeURIComponent(`${savesPath}/${file.name}`)}`, download: file.name },
      el('span', { className: 'kind', textContent: t('File') }), el('span', { className: 'grow dynamic', textContent: file.name }),
      el('span', { className: 'size', textContent: bytes(file.size) })));
  }
  if (!rows.length) rows.push(el('p', { className: 'muted', textContent: t('This folder is empty.') }));
  $('#saves-list').replaceChildren(...rows);
}

// A ZIP read in the browser: its central directory, each file stored or deflated (DecompressionStream). ZIP64 is not
// read: the PS5's own backups never use it (src/pw_webui_http.cpp).
async function readZip(file) {
  const data = new DataView(await file.arrayBuffer());
  let end = -1;
  for (let at = data.byteLength - 22; at >= Math.max(0, data.byteLength - 65557); --at)
    if (data.getUint32(at, true) === 0x06054b50) { end = at; break; }
  if (end < 0) throw new Error(t('This file is not a .zip backup.'));
  const count = data.getUint16(end + 10, true);
  let at = data.getUint32(end + 16, true);
  const decoder = new TextDecoder();
  const files = [];
  for (let i = 0; i < count; ++i) {
    if (at + 46 > data.byteLength || data.getUint32(at, true) !== 0x02014b50) throw new Error(t('This .zip is damaged.'));
    const method = data.getUint16(at + 10, true), packed = data.getUint32(at + 20, true);
    const nameLength = data.getUint16(at + 28, true), extra = data.getUint16(at + 30, true), comment = data.getUint16(at + 32, true);
    const local = data.getUint32(at + 42, true);
    const name = decoder.decode(new Uint8Array(data.buffer, at + 46, nameLength));
    at += 46 + nameLength + extra + comment;
    if (name.endsWith('/')) continue;
    const start = local + 30 + data.getUint16(local + 26, true) + data.getUint16(local + 28, true);
    let blob = file.slice(start, start + packed);
    if (method === 8) blob = await new Response(blob.stream().pipeThrough(new DecompressionStream('deflate-raw'))).blob();
    else if (method !== 0) throw new Error(t('This .zip uses a kind of compression this page cannot open.'));
    files.push(Object.assign(new File([blob], name.split('/').pop()), { zipPath: name.replace(/\\/g, '/') }));
  }
  return files;
}
async function restoreZip(file) {
  $('#saves-zip-input').value = '';
  if (!file) return;
  let files;
  try {
    files = await readZip(file);
  } catch (error) {
    toast(error.message, true);
    return;
  }
  if (!files.length) { toast(t('This .zip is empty.'), true); return; }
  // A backup from this page names each file from C:\ (users/<user>/<folder>/...): nothing is written unless all do
  if (files.some((one) => !/^users\/[^/]+\/[^/]+\/./.test(one.zipPath))) {
    toast(t('This .zip was not made by this page: it does not say where its files go on your PS5.'), true);
    return;
  }
  if (!confirm(t('Put the {0} files of this backup back on your PS5? Files with the same names are replaced.', files.length))) return;
  stopRequested = false;
  $('#saves-tube').hidden = false;
  try {
    await sendFiles($('#saves-prefix').value || 'mihawk', files, (one) => one.zipPath, (done, total) => {
      $('#saves-bar').style.width = `${Math.min(100, (100 * done) / Math.max(total, 1))}%`;
      $('#saves-status').textContent = t('{0} of {1} put back', bytes(done), bytes(total));
    });
    $('#saves-status').textContent = t('Done: the backup is back on your PS5.');
    toast(t('Done: the backup is back on your PS5.'));
  } catch (error) {
    $('#saves-status').textContent = error.message;
    toast(error.message, true);
  }
}

// ---- settings ----
function renderSettings() {
  $('#game-mode').checked = settings.game_mode;
  $('#high-refresh').checked = settings.high_refresh;
  $('#logging').checked = settings.logging;
  $('#controller').replaceChildren(...settings.choices.controllers.map((choice) => {
    const button = el('button', { type: 'button', textContent: t(choice.label), className: choice.id === settings.controller ? 'on' : '' });
    button.addEventListener('click', () => saveSetting({ controller: choice.id }));
    return button;
  }));
  $('#look-card').hidden = !settings.choices.themes.length;
  for (const key of ['theme', 'music'])
    $(`#${key}`).replaceChildren(...settings.choices[key === 'theme' ? 'themes' : 'music'].map((choice) =>
      el('option', { value: choice.id, textContent: t(choice.label), selected: choice.id === settings[key] })));
  for (const action of ['left', 'right', 'middle']) {
    $(`#mouse-${action}`).replaceChildren(...settings.choices.buttons.map((button) =>
      el('option', { value: button.id, textContent: t(button.label), selected: button.id === settings.mouse[action] })));
  }
}
async function loadSettings() {
  try {
    settings = await api('/api/settings');
    renderSettings();
  } catch (error) {
    toast(error.message, true);
  }
}
async function saveSetting(change, said = t('Saved. Your games use it from the next time they start.')) {
  try {
    settings = await post('/api/settings', change);
    renderSettings();
    toast(said);
  } catch (error) {
    toast(error.message, true);
    loadSettings();
  }
}

// ---- help: the logs and the bug report ----
let logName = '';
function loadHelp() {
  if ($('#logs-box').open) loadLogs();
}
async function loadLogs() {
  try {
    const { logs } = await api('/api/logs');
    $('#log-list').replaceChildren(...(logs.length ? logs.map((log) => {
      const button = el('button', { type: 'button', className: log.name === logName ? 'on' : '' },
        el('span', { className: 'dynamic', textContent: log.name }), el('span', { className: 'size', textContent: bytes(log.size) }));
      button.addEventListener('click', () => openLog(log.name));
      return button;
    }) : [el('p', { className: 'muted', textContent: t('No logs yet.') })]));
  } catch (error) {
    toast(error.message, true);
  }
}
async function openLog(name) {
  logName = name;
  $('#log-name').textContent = name;
  $('#log-text').textContent = t('Reading…');
  try {
    $('#log-text').textContent = await api(`/api/log?name=${encodeURIComponent(name)}`);
    $('#log-save').disabled = false;
  } catch (error) {
    $('#log-text').textContent = error.message;
  }
  for (const button of document.querySelectorAll('#log-list button')) button.classList.toggle('on', button.firstChild.textContent === name);
}
function saveLog() {
  const link = el('a', { download: logName.replace(/\//g, '-'), href: URL.createObjectURL(new Blob([$('#log-text').textContent], { type: 'text/plain' })) });
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}

// ---- the language picker: a flag in the header, the guided setup's languages, each in its own words ----
function languagePicker() {
  const languages = (window.MIHAWK_I18N || {}).languages || [];
  const button = $('#language-button'), list = $('#language-list');
  const close = () => { list.hidden = true; button.setAttribute('aria-expanded', 'false'); };
  const mark = () => {
    const current = languages.find((one) => one.tag === language) || languages[0];
    if (current) $('#language-flag').src = `/flags/${current.flag}.svg`;
    for (const item of list.children) item.classList.toggle('on', item.lang === language);
  };
  list.replaceChildren(...languages.map((one) => {
    const item = el('button', { type: 'button', lang: one.tag },
      el('img', { src: `/flags/${one.flag}.svg`, alt: '', width: 28, height: 21 }), el('span', { className: 'dynamic', textContent: one.name }));
    item.addEventListener('click', () => {
      try { localStorage.setItem('mihawk-language', one.tag); } catch (error) { /* this page only */ }
      language = useLanguage(one.tag);
      mark();
      close();
      show();
    });
    return item;
  }));
  button.addEventListener('click', (event) => {
    event.stopPropagation();
    list.hidden = !list.hidden;
    button.setAttribute('aria-expanded', String(!list.hidden));
    if (!list.hidden) (list.querySelector('.on') || list.firstChild).focus();
  });
  document.addEventListener('click', (event) => { if (!event.target.closest('.language')) close(); });
  document.addEventListener('keydown', (event) => { if (event.key === 'Escape' && !list.hidden) { close(); button.focus(); } });
  mark();
}

// ---- start ----
let language = 'en-US';
async function connect() {
  try {
    const status = await refreshToken();
    language = useLanguage(storedLanguage() || status.language || '');
    $('#about-version').textContent = t('Version {0} · MihawkOS runtime {1}', status.version, status.runtime || t('not installed'));
    return true;
  } catch (error) {
    language = useLanguage(storedLanguage());
    return false;
  }
}
async function start() {
  await connect();
  languagePicker();
  $('#retry').addEventListener('click', async () => { if (await connect()) { settings = null; show(); } });
  window.addEventListener('hashchange', show);
  // A game's settings
  $('#editor-form').addEventListener('submit', saveEditor);
  $('#editor-cancel').addEventListener('click', () => $('#editor').close());
  $('#editor-delete').addEventListener('click', deleteEntry);
  $('#browse').addEventListener('click', openPicker);
  $('#editor-form').executable.addEventListener('input', showProgram);
  $('#picker-up').addEventListener('click', () => browse(pickerPath.split('/').slice(0, -1).join('/') || 'Games'));
  $('#picker-close').addEventListener('click', () => $('#picker').close());
  $('#media-find').addEventListener('click', findArt);
  // The search field has a form of its own: Enter in it searches, never submits the game
  $('#media-search').addEventListener('submit', (event) => { event.preventDefault(); findArt(); });
  $('#media-own').addEventListener('change', (event) => ownPicture(event.target.files[0]));
  $('#media-remove').addEventListener('click', removeArt);
  // Add a game
  $('#folder-input').addEventListener('change', (event) => chooseFiles(event.target.files));
  const drop = $('#drop');
  drop.addEventListener('dragover', () => drop.classList.add('over'));
  drop.addEventListener('dragleave', () => drop.classList.remove('over'));
  drop.addEventListener('drop', () => drop.classList.remove('over'));
  $('#upload-start').addEventListener('click', () => { if (!sending) sendFolder(); });
  $('#upload-cancel').addEventListener('click', () => { if (sending) stopRequested = true; else wizardStep(1); });
  $('#added-setup').addEventListener('click', setUpCopied);
  $('#added-another').addEventListener('click', () => wizardStep(1));
  wizardStep(1);
  // Home screen
  $('#tile-game').addEventListener('change', () => {
    $('#tile-name').value = $('#tile-game').selectedOptions[0] ? $('#tile-game').selectedOptions[0].textContent : '';
    loadTileArt();
  });
  tileControls();
  $('#tile-make').addEventListener('click', makeTile);
  $('#smp-check').addEventListener('click', loadTiles);
  // Saves
  $('#saves-prefix').addEventListener('change', () => { savesPath = ''; loadSaves(); });
  $('#saves-browser').addEventListener('toggle', () => { if ($('#saves-browser').open) browseSaves(savesPath || userRoot); });
  $('#saves-up').addEventListener('click', () => browseSaves(savesPath.split('/').slice(0, -1).join('/') || 'users'));
  $('#saves-zip-input').addEventListener('change', (event) => restoreZip(event.target.files[0]));
  // Settings
  for (const [id, key] of [['#game-mode', 'game_mode'], ['#high-refresh', 'high_refresh'], ['#logging', 'logging']])
    $(id).addEventListener('change', (event) => saveSetting({ [key]: event.target.checked }));
  for (const action of ['left', 'right', 'middle'])
    $(`#mouse-${action}`).addEventListener('change', (event) => saveSetting({ mouse: { [action]: event.target.value } }));
  for (const key of ['theme', 'music'])
    $(`#${key}`).addEventListener('change', (event) => saveSetting({ [key]: event.target.value }, t('Saved. Your PS5 shows it now.')));
  // Help
  $('#logs-box').addEventListener('toggle', () => { if ($('#logs-box').open) loadLogs(); });
  $('#logs-refresh').addEventListener('click', () => { loadLogs(); if (logName) openLog(logName); });
  $('#log-save').addEventListener('click', saveLog);
  show();
}
start();
