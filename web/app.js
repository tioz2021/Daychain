/* habits — frontend */

const ICONS = {
  dot: "•",
  book: "📖",
  water: "💧",
  run: "🏃",
  gym: "🏋️",
  sleep: "😴",
  meditate: "🧘",
  code: "💻",
  music: "🎵",
  food: "🍎",
  study: "🎓",
  walk: "🚶",
  journal: "✍️",
  star: "⭐",
  "no-smoke": "🚭",
  shield: "🛡️",
  heart: "❤️",
};

const COLORS = ["#4f8cff", "#22c55e", "#f59e0b", "#ef4444", "#a855f7", "#06b6d4", "#ec4899", "#64748b"];

const LANGS = [
  ["en", "English"],
  ["ru", "Русский"],
  ["uk", "Українська"],
];

const state = {
  lang: "en",
  dict: null,
  fallback: null,
  theme: "system",
  view: "today",
  date: todayStr(),
  period: 30,
  statsHabit: null,
  health: { version: "", data_dir: "" },
};

/* ---------- helpers ---------- */

function todayStr() {
  const d = new Date();
  const m = String(d.getMonth() + 1).padStart(2, "0");
  const day = String(d.getDate()).padStart(2, "0");
  return `${d.getFullYear()}-${m}-${day}`;
}

function shiftDate(str, days) {
  const [y, m, d] = str.split("-").map(Number);
  const dt = new Date(y, m - 1, d + days);
  const mm = String(dt.getMonth() + 1).padStart(2, "0");
  const dd = String(dt.getDate()).padStart(2, "0");
  return `${dt.getFullYear()}-${mm}-${dd}`;
}

function esc(s) {
  return String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#39;",
  }[c]));
}

function t(key, vars) {
  let v = state.dict && state.dict[key];
  if (v === undefined && state.fallback) v = state.fallback[key];
  if (v === undefined) return key;
  if (typeof v === "string" && vars) {
    for (const [k, val] of Object.entries(vars)) v = v.split(`{${k}}`).join(val);
  }
  return v;
}

function plural(n, forms) {
  if (!forms || typeof forms !== "object") return "";
  if (state.lang === "en") return n === 1 ? forms.one : forms.other || forms.many;
  const n10 = n % 10;
  const n100 = n % 100;
  if (n10 === 1 && n100 !== 11) return forms.one;
  if (n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14)) return forms.few;
  return forms.many;
}

function locale() {
  return { en: "en-GB", ru: "ru-RU", uk: "uk-UA" }[state.lang] || "en-GB";
}

function sentenceCase(s) {
  return s ? s.charAt(0).toUpperCase() + s.slice(1) : s;
}

function fmtLongDate(str) {
  const [y, m, d] = str.split("-").map(Number);
  return sentenceCase(
    new Intl.DateTimeFormat(locale(), {
      weekday: "long",
      day: "numeric",
      month: "long",
    }).format(new Date(y, m - 1, d))
  );
}

function fmtShortDate(str) {
  const [y, m, d] = str.split("-").map(Number);
  return new Intl.DateTimeFormat(locale(), { day: "numeric", month: "short" }).format(
    new Date(y, m - 1, d)
  );
}

function icon(key) {
  return ICONS[key] || ICONS.dot;
}

function parseIsoMs(iso) {
  let t = Date.parse(iso);
  if (isNaN(t)) t = Date.parse(String(iso).replace(/\.\d+/, ""));
  return t;
}

function fmtDateTime(iso) {
  const d = new Date(parseIsoMs(iso));
  if (isNaN(d.getTime())) return iso;
  return new Intl.DateTimeFormat(locale(), { dateStyle: "medium", timeStyle: "short" }).format(d);
}

function toLocalInput(iso) {
  const d = new Date(iso);
  const pad = (n) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(
    d.getHours()
  )}:${pad(d.getMinutes())}`;
}

function formatDuration(seconds) {
  let secs = Math.max(0, Math.floor(seconds));
  const days = Math.floor(secs / 86400);
  const hh = String(Math.floor((secs % 86400) / 3600)).padStart(2, "0");
  const mm = String(Math.floor((secs % 3600) / 60)).padStart(2, "0");
  const ss = String(secs % 60).padStart(2, "0");
  const clock = `${hh}:${mm}:${ss}`;
  if (days > 0) return `${days} ${plural(days, t("unit_days"))} ${clock}`;
  return clock;
}

/* Live counters on the "Quitting" page. */
let tickerId = null;

function stopTicker() {
  if (tickerId) {
    clearInterval(tickerId);
    tickerId = null;
  }
}

function startTicker() {
  stopTicker();
  const update = () => {
    const now = Date.now() / 1000;
    document.querySelectorAll("[data-quit-start]").forEach((el) => {
      const start = Number(el.dataset.quitStart);
      const best = Number(el.dataset.quitBest || 0);
      const elapsed = now - start;
      el.textContent = formatDuration(elapsed);
      const card = el.closest(".quit-card");
      const bestEl = card && card.querySelector(".quit-best");
      if (bestEl) bestEl.textContent = formatDuration(Math.max(best, elapsed));
    });
  };
  update();
  tickerId = setInterval(update, 1000);
}

function toast(message, isError) {
  const el = document.getElementById("toast");
  el.textContent = message;
  el.className = "toast show" + (isError ? " error" : "");
  el.hidden = false;
  clearTimeout(toast._timer);
  toast._timer = setTimeout(() => {
    el.className = "toast";
  }, 2600);
}

/* ---------- api ---------- */

async function api(method, path, body) {
  let res;
  try {
    res = await fetch(path, {
      method,
      headers: {
        "X-Requested-With": "habits",
        ...(body !== undefined ? { "Content-Type": "application/json" } : {}),
      },
      body: body !== undefined ? JSON.stringify(body) : undefined,
    });
  } catch (e) {
    throw Object.assign(new Error(t("error_network")), { code: "__network" });
  }
  if (res.status === 204) return null;
  const text = await res.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = null;
  }
  if (!res.ok) {
    const err = (data && data.error) || {};
    throw Object.assign(new Error(err.message || `HTTP ${res.status}`), {
      code: err.code || "generic",
      status: res.status,
    });
  }
  return data;
}

function errorText(e) {
  if (!e) return t("error_generic");
  if (e.code === "__network") return t("error_network");
  const key = `err_${e.code}`;
  const localized = state.dict && state.dict[key];
  if (localized) return localized;
  return e.message || t("error_generic");
}

/* ---------- i18n ---------- */

async function loadDict(lang) {
  try {
    const mod = await import(`./i18n/${lang}.js`);
    return mod.default;
  } catch {
    const mod = await import("./i18n/en.js");
    return mod.default;
  }
}

async function setLang(lang, doRender = true) {
  if (!LANGS.some(([code]) => code === lang)) lang = "en";
  state.lang = lang;
  state.dict = await loadDict(lang);
  if (!state.fallback) state.fallback = lang === "en" ? state.dict : await loadDict("en");
  localStorage.setItem("lang", lang);
  document.documentElement.lang = lang;
  applyStaticTranslations();
  updateNav();
  if (doRender) render();
}

function applyStaticTranslations() {
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    el.textContent = t(el.dataset.i18n);
  });
  document.title = t("app_name");
}

function updateNav() {
  document.querySelectorAll(".nav a").forEach((a) => {
    a.classList.toggle("active", a.dataset.view === state.view);
  });
}

/* ---------- theme ---------- */

function applyTheme(theme) {
  state.theme = theme;
  if (theme === "system") document.documentElement.removeAttribute("data-theme");
  else document.documentElement.dataset.theme = theme;
  localStorage.setItem("theme", theme);
}

/* ---------- rendering ---------- */

const main = () => document.getElementById("main");

function render() {
  updateNav();
  stopTicker();
  if (state.view === "today") renderToday();
  else if (state.view === "habits") renderHabits();
  else if (state.view === "quits") renderQuits();
  else if (state.view === "stats") renderStats();
  else if (state.view === "settings") renderSettings();
}

function pageHead(title, actionsHtml) {
  return `<div class="page-head"><h1 class="page-title">${esc(title)}</h1>
    <div class="page-actions">${actionsHtml || ""}</div></div>`;
}

/* ---------- today ---------- */

async function renderToday() {
  main().innerHTML = `<div class="page"><div class="empty">…</div></div>`;
  let data;
  try {
    data = await api("GET", `/api/v1/today?date=${state.date}`);
  } catch (e) {
    main().innerHTML = `<div class="page"><div class="empty">${esc(errorText(e))}</div></div>`;
    return;
  }

  const isToday = state.date === todayStr();
  const rows = data.items
    .map((item) => {
      const h = item.habit;
      const e = item.entry;
      const st = e ? e.status : "";
      const checkClass = st === "done" ? "done" : st === "skipped" ? "skipped" : "";
      const streakUnit = item.streak.unit === "weeks" ? "streak_weeks" : "streak_days";
      const streakText =
        item.streak.current > 0
          ? `${item.streak.current} ${plural(item.streak.current, t(streakUnit))}`
          : "";
      const noteChip = e && e.note ? `<span class="chip" title="${esc(e.note)}">📝</span>` : "";
      return `<div class="habit-card ${st}" data-id="${h.id}" style="--accent:${esc(h.color)}">
        <div class="habit-icon" style="color:${esc(h.color)}">${icon(h.icon)}</div>
        <div class="habit-main">
          <div class="habit-title">${esc(h.name)}</div>
          <div class="habit-meta">
            ${streakText ? `<span class="chip streak">🔥 ${esc(streakText)}</span>` : ""}
            <span class="chip" title="${esc(t("week_progress", { done: item.week.done, quota: item.week.quota }))}">${item.week.done} / ${item.week.quota}</span>
            ${noteChip}
            ${h.category ? `<span class="chip">${esc(h.category)}</span>` : ""}
          </div>
        </div>
        <div class="habit-side">
          <button class="round-check ${checkClass}" data-action="toggle" data-id="${h.id}"
            title="${esc(t("mark_done"))}" aria-label="${esc(t("mark_done"))}">
            ${st === "done" ? checkSvg() : st === "skipped" ? skipSvg() : ""}
          </button>
          <button class="btn-icon ${st === "skipped" ? "on" : ""}" data-action="skip" data-id="${h.id}"
            title="${esc(t("mark_skipped"))}" aria-label="${esc(t("mark_skipped"))}">${skipSvg()}</button>
          <button class="btn-icon" data-action="note" data-id="${h.id}"
            title="${esc(t("add_note"))}" aria-label="${esc(t("add_note"))}">${noteSvg()}</button>
        </div>
      </div>`;
    })
    .join("");

  main().innerHTML = `<div class="page">
    ${pageHead(
      t("nav_today"),
      `<button class="btn" data-action="prev" ${""}>${arrowLeft()}</button>
       <button class="btn" data-action="next" ${isToday ? "disabled" : ""}>${arrowRight()}</button>`
    )}
    <div class="date-bar">
      <span class="date-label">${esc(fmtLongDate(state.date))}</span>
      ${isToday ? `<span class="date-sub">${esc(t("today"))}</span>` : `<button class="btn btn-ghost" data-action="goto-today">${esc(t("today"))}</button>`}
    </div>
    ${rows ? `<div class="habit-list">${rows}</div>` : `<div class="empty">${esc(t("today_empty"))}<div style="margin-top:14px"><button class="btn btn-primary" data-action="new-habit">${esc(t("new_habit"))}</button></div></div>`}
  </div>`;

  main().querySelectorAll("[data-action]").forEach((el) => {
    el.addEventListener("click", onTodayAction);
  });
}

function checkSvg() {
  return `<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.5l4.5 4.5L19 7"/></svg>`;
}
function skipSvg() {
  return `<svg viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="8.5"/><path d="M8 16L16 8"/></svg>`;
}
function noteSvg() {
  return `<svg viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 20h4L19.5 8.5a2.1 2.1 0 0 0-3-3L5 17z"/><path d="M14 7l3 3"/></svg>`;
}
function arrowLeft() {
  return `<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M15 5l-7 7 7 7"/></svg>`;
}
function arrowRight() {
  return `<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 5l7 7-7 7"/></svg>`;
}
function archiveSvg() {
  return `<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><rect x="3.5" y="4.5" width="17" height="4" rx="1.2"/><path d="M5 8.5v10a1.5 1.5 0 0 0 1.5 1.5h11a1.5 1.5 0 0 0 1.5-1.5v-10"/><path d="M9.5 13h5"/></svg>`;
}
function restoreSvg() {
  return `<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M4 9a8 8 0 1 1 2 6.5"/><path d="M4 4v5h5"/></svg>`;
}
function trashSvg() {
  return `<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7h16M9 7V5h6v2M7 7l1 13h8l1-13"/></svg>`;
}

async function onTodayAction(ev) {
  const btn = ev.currentTarget;
  const action = btn.dataset.action;
  if (action === "prev") {
    state.date = shiftDate(state.date, -1);
    renderToday();
  } else if (action === "next") {
    if (state.date < todayStr()) {
      state.date = shiftDate(state.date, 1);
      renderToday();
    }
  } else if (action === "goto-today") {
    state.date = todayStr();
    renderToday();
  } else if (action === "new-habit") {
    openHabitModal(null);
  } else if (action === "toggle" || action === "skip" || action === "note") {
    await handleRowAction(action, Number(btn.dataset.id));
  }
}

async function handleRowAction(action, id) {
  try {
    if (action === "toggle") {
      const data = await api("GET", `/api/v1/habits/${id}/entries?from=${state.date}&to=${state.date}`);
      const entry = data.entries[0];
      if (entry && entry.status === "done") {
        await api("DELETE", `/api/v1/habits/${id}/entries/${state.date}`);
      } else {
        await api("PUT", `/api/v1/habits/${id}/entries/${state.date}`, {
          status: "done",
          note: entry ? entry.note : "",
        });
      }
    } else if (action === "skip") {
      const data = await api("GET", `/api/v1/habits/${id}/entries?from=${state.date}&to=${state.date}`);
      const entry = data.entries[0];
      if (entry && entry.status === "skipped") {
        await api("DELETE", `/api/v1/habits/${id}/entries/${state.date}`);
      } else {
        await api("PUT", `/api/v1/habits/${id}/entries/${state.date}`, {
          status: "skipped",
          note: entry ? entry.note : "",
        });
      }
    } else if (action === "note") {
      openNoteModal(id);
      return;
    }
    renderToday();
  } catch (e) {
    toast(errorText(e), true);
  }
}

/* ---------- note modal ---------- */

async function openNoteModal(id) {
  const data = await api("GET", `/api/v1/habits/${id}/entries?from=${state.date}&to=${state.date}`).catch(
    () => ({ entries: [] })
  );
  const entry = data.entries[0];
  const current = entry ? entry.note : "";
  const status = entry ? entry.status : "done";

  const root = document.getElementById("modal-root");
  root.innerHTML = `<div class="modal-backdrop" data-close>
    <div class="modal" role="dialog" aria-modal="true" aria-label="${esc(t("add_note"))}">
      <div class="modal-head">
        <h2>${esc(t("add_note"))}</h2>
        <button class="btn-icon" data-close aria-label="${esc(t("close"))}">✕</button>
      </div>
      <div class="field">
        <textarea class="textarea" id="note-text" maxlength="500"
          placeholder="${esc(t("note_placeholder"))}">${esc(current)}</textarea>
      </div>
      <div class="modal-actions">
        <button class="btn" data-close>${esc(t("cancel"))}</button>
        <button class="btn btn-primary" id="note-save">${esc(t("save"))}</button>
      </div>
    </div>
  </div>`;

  root.querySelectorAll("[data-close]").forEach((el) => {
    el.addEventListener("click", (ev) => {
      if (ev.target === el) closeModal();
    });
  });
  const input = root.querySelector("#note-text");
  input.focus();
  input.setSelectionRange(input.value.length, input.value.length);

  const save = async () => {
    try {
      if (!entry && !input.value.trim()) {
        closeModal();
        return;
      }
      await api("PUT", `/api/v1/habits/${id}/entries/${state.date}`, {
        status,
        note: input.value,
      });
      closeModal();
      renderToday();
    } catch (e) {
      toast(errorText(e), true);
    }
  };
  root.querySelector("#note-save").addEventListener("click", save);
  root.querySelector(".modal").addEventListener("keydown", (ev) => {
    if (ev.key === "Enter" && (ev.ctrlKey || ev.metaKey)) save();
  });
}

function closeModal() {
  document.getElementById("modal-root").innerHTML = "";
}

/* ---------- habits page ---------- */

async function renderHabits() {
  main().innerHTML = `<div class="page"><div class="empty">…</div></div>`;
  let data;
  try {
    data = await api("GET", "/api/v1/habits?archived=1");
  } catch (e) {
    main().innerHTML = `<div class="page"><div class="empty">${esc(errorText(e))}</div></div>`;
    return;
  }
  const active = data.habits.filter((h) => !h.archived_at);
  const archived = data.habits.filter((h) => h.archived_at);

  const line = (h, isArchived) => `<div class="habit-line ${isArchived ? "archived" : ""}" data-id="${h.id}">
      <div class="habit-icon" style="color:${esc(h.color)}">${icon(h.icon)}</div>
      <div class="grow">
        <div class="name">${esc(h.name)}</div>
        <div class="meta">${esc(scheduleLabel(h))}${h.category ? " · " + esc(h.category) : ""}</div>
      </div>
      <button class="btn-icon" data-action="edit" data-id="${h.id}" title="${esc(t("edit"))}" aria-label="${esc(t("edit"))}">${noteSvg()}</button>
      ${
        isArchived
          ? `<button class="btn-icon" data-action="unarchive" data-id="${h.id}" title="${esc(t("unarchive"))}" aria-label="${esc(t("unarchive"))}">${restoreSvg()}</button>`
          : `<button class="btn-icon" data-action="archive" data-id="${h.id}" title="${esc(t("archive"))}" aria-label="${esc(t("archive"))}">${archiveSvg()}</button>`
      }
      <button class="btn-icon" data-action="delete" data-id="${h.id}" title="${esc(t("delete"))}" aria-label="${esc(t("delete"))}">${trashSvg()}</button>
    </div>`;

  main().innerHTML = `<div class="page">
    ${pageHead(t("habits_title"), `<button class="btn btn-primary" data-action="new">＋ ${esc(t("new_habit"))}</button>`)}
    ${active.length ? active.map((h) => line(h, false)).join("") : `<div class="empty">${esc(t("habits_empty"))}</div>`}
    ${archived.length ? `<div class="section-label">${esc(t("habits_archived"))}</div>${archived.map((h) => line(h, true)).join("")}` : ""}
  </div>`;

  main().querySelectorAll("[data-action]").forEach((el) => el.addEventListener("click", onHabitAction));
  state._habits = data.habits;
}

function scheduleLabel(h) {
  const s = h.schedule;
  if (s.kind === "daily") return t("schedule_daily");
  if (s.kind === "weekly_count") return t("schedule_weekly_count") + ` · ${s.times}×`;
  const days = t("weekdays_short");
  return s.days.map((d) => days[d - 1]).join(", ");
}

async function onHabitAction(ev) {
  const btn = ev.currentTarget;
  const id = Number(btn.dataset.id);
  const habit = (state._habits || []).find((h) => h.id === id);
  try {
    if (btn.dataset.action === "new") return openHabitModal(null);
    if (btn.dataset.action === "edit") return openHabitModal(habit);
    if (btn.dataset.action === "archive") {
      await api("POST", `/api/v1/habits/${id}/archive`);
      renderHabits();
      return;
    }
    if (btn.dataset.action === "unarchive") {
      await api("POST", `/api/v1/habits/${id}/unarchive`);
      renderHabits();
      return;
    }
    if (btn.dataset.action === "delete") {
      if (!confirm(t("delete_confirm"))) return;
      await api("DELETE", `/api/v1/habits/${id}`);
      toast(t("toast_deleted"));
      renderHabits();
    }
  } catch (e) {
    toast(errorText(e), true);
  }
}

/* ---------- habit form ---------- */

function openHabitModal(habit) {
  const isEdit = Boolean(habit);
  const draft = {
    color: habit ? habit.color : COLORS[0],
    icon: habit ? habit.icon : "dot",
    kind: habit ? habit.schedule.kind : "daily",
    days: habit && habit.schedule.days ? habit.schedule.days.slice() : [1, 2, 3, 4, 5, 6, 7],
    times: habit && habit.schedule.times ? habit.schedule.times : 3,
  };

  const colorSwatches = COLORS.map(
    (c) => `<button type="button" class="swatch ${c === draft.color ? "sel" : ""}" data-color="${c}"
      style="background:${c}" aria-label="${c}"></button>`
  ).join("");

  const iconOptions = Object.keys(ICONS)
    .map(
      (k) => `<button type="button" class="icon-opt ${k === draft.icon ? "sel" : ""}" data-icon="${k}"
        title="${k}">${ICONS[k]}</button>`
    )
    .join("");

  const dayOptions = t("weekdays_short")
    .map(
      (d, i) => `<button type="button" class="day-opt ${draft.days.includes(i + 1) ? "sel" : ""}"
        data-day="${i + 1}">${d}</button>`
    )
    .join("");

  const root = document.getElementById("modal-root");
  root.innerHTML = `<div class="modal-backdrop" data-close>
    <div class="modal" role="dialog" aria-modal="true">
      <div class="modal-head">
        <h2>${esc(isEdit ? t("form_title_edit") : t("form_title_new"))}</h2>
        <button class="btn-icon" data-close aria-label="${esc(t("close"))}">✕</button>
      </div>
      <form id="habit-form" novalidate>
        <div class="field">
          <label for="f-name">${esc(t("form_name"))}</label>
          <input id="f-name" name="name" class="input" maxlength="80" autocomplete="off"
            placeholder="${esc(t("form_name_placeholder"))}" value="${esc(habit ? habit.name : "")}">
        </div>
        <div class="field">
          <label for="f-desc">${esc(t("form_description"))}</label>
          <textarea id="f-desc" name="description" class="textarea" maxlength="2000">${esc(habit ? habit.description : "")}</textarea>
        </div>
        <div class="row-2">
          <div class="field">
            <label for="f-cat">${esc(t("form_category"))}</label>
            <input id="f-cat" name="category" class="input" maxlength="40" value="${esc(habit && habit.category ? habit.category : "")}">
          </div>
          <div class="field">
            <label for="f-start">${esc(t("form_start_date"))}</label>
            <input id="f-start" name="start_date" type="date" class="input" value="${esc(habit ? habit.start_date : todayStr())}">
          </div>
        </div>
        <div class="field">
          <span class="field-label">${esc(t("form_color"))}</span>
          <div class="swatches">${colorSwatches}</div>
        </div>
        <div class="field">
          <span class="field-label">${esc(t("form_icon"))}</span>
          <div class="icon-grid">${iconOptions}</div>
        </div>
        <div class="field">
          <label for="f-kind">${esc(t("form_schedule"))}</label>
          <select id="f-kind" name="kind" class="select">
            <option value="daily" ${draft.kind === "daily" ? "selected" : ""}>${esc(t("schedule_daily"))}</option>
            <option value="weekdays" ${draft.kind === "weekdays" ? "selected" : ""}>${esc(t("schedule_weekdays"))}</option>
            <option value="weekly_count" ${draft.kind === "weekly_count" ? "selected" : ""}>${esc(t("schedule_weekly_count"))}</option>
          </select>
        </div>
        <div class="field" id="f-days-field" style="display:${draft.kind === "weekdays" ? "block" : "none"}">
          <span class="field-label">${esc(t("form_days"))}</span>
          <div class="day-picker">${dayOptions}</div>
        </div>
        <div class="field" id="f-times-field" style="display:${draft.kind === "weekly_count" ? "block" : "none"}">
          <label for="f-times">${esc(t("form_times"))}</label>
          <input id="f-times" name="times" type="number" min="1" max="7" class="input" value="${draft.times}">
        </div>
        <div class="form-error" id="form-error"></div>
        <div class="modal-actions">
          ${isEdit ? `<button type="button" class="btn btn-danger danger" id="f-delete">${esc(t("delete"))}</button>` : ""}
          <button type="button" class="btn" data-close>${esc(t("cancel"))}</button>
          <button type="submit" class="btn btn-primary">${esc(t("save"))}</button>
        </div>
      </form>
    </div>
  </div>`;

  root.querySelectorAll("[data-close]").forEach((el) => {
    el.addEventListener("click", (ev) => {
      if (ev.target === el) closeModal();
    });
  });

  root.querySelectorAll(".swatch").forEach((el) =>
    el.addEventListener("click", () => {
      draft.color = el.dataset.color;
      root.querySelectorAll(".swatch").forEach((s) => s.classList.toggle("sel", s === el));
    })
  );
  root.querySelectorAll(".icon-opt").forEach((el) =>
    el.addEventListener("click", () => {
      draft.icon = el.dataset.icon;
      root.querySelectorAll(".icon-opt").forEach((s) => s.classList.toggle("sel", s === el));
    })
  );
  root.querySelectorAll(".day-opt").forEach((el) =>
    el.addEventListener("click", () => {
      const day = Number(el.dataset.day);
      if (draft.days.includes(day)) {
        if (draft.days.length === 1) return;
        draft.days = draft.days.filter((d) => d !== day);
        el.classList.remove("sel");
      } else {
        draft.days.push(day);
        draft.days.sort((a, b) => a - b);
        el.classList.add("sel");
      }
    })
  );

  const kindSelect = root.querySelector("#f-kind");
  kindSelect.addEventListener("change", () => {
    draft.kind = kindSelect.value;
    root.querySelector("#f-days-field").style.display = draft.kind === "weekdays" ? "block" : "none";
    root.querySelector("#f-times-field").style.display = draft.kind === "weekly_count" ? "block" : "none";
  });

  const errorEl = root.querySelector("#form-error");
  const form = root.querySelector("#habit-form");
  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const name = root.querySelector("#f-name").value.trim();
    if (!name) {
      errorEl.textContent = t("form_name_required");
      return;
    }
    const body = {
      name,
      description: root.querySelector("#f-desc").value,
      category: root.querySelector("#f-cat").value.trim() || null,
      color: draft.color,
      icon: draft.icon,
      start_date: root.querySelector("#f-start").value || todayStr(),
    };
    if (draft.kind === "weekdays") body.schedule = { kind: "weekdays", days: draft.days };
    else if (draft.kind === "weekly_count")
      body.schedule = { kind: "weekly_count", times: Number(root.querySelector("#f-times").value) || 1 };
    else body.schedule = { kind: "daily" };

    try {
      if (isEdit) await api("PATCH", `/api/v1/habits/${habit.id}`, body);
      else await api("POST", "/api/v1/habits", body);
      closeModal();
      toast(t("toast_saved"));
      if (state.view === "habits") renderHabits();
      else renderToday();
    } catch (e) {
      errorEl.textContent = errorText(e);
    }
  });

  if (isEdit) {
    root.querySelector("#f-delete").addEventListener("click", async () => {
      if (!confirm(t("delete_confirm"))) return;
      try {
        await api("DELETE", `/api/v1/habits/${habit.id}`);
        closeModal();
        toast(t("toast_deleted"));
        renderHabits();
      } catch (e) {
        errorEl.textContent = errorText(e);
      }
    });
  }

  const nameInput = root.querySelector("#f-name");
  nameInput.focus();
  nameInput.setSelectionRange(nameInput.value.length, nameInput.value.length);
  root.querySelector(".modal").addEventListener("keydown", (ev) => {
    if (ev.key === "Escape") closeModal();
  });
}

/* ---------- stats ---------- */

async function renderStats() {
  main().innerHTML = `<div class="page"><div class="empty">…</div></div>`;
  let habitsData;
  try {
    habitsData = await api("GET", "/api/v1/habits?archived=1");
  } catch (e) {
    main().innerHTML = `<div class="page"><div class="empty">${esc(errorText(e))}</div></div>`;
    return;
  }
  const habits = habitsData.habits;
  if (!habits.length) {
    main().innerHTML = `<div class="page">${pageHead(t("stats_title"), "")}<div class="empty">${esc(t("habits_empty"))}</div></div>`;
    return;
  }
  if (!state.statsHabit || !habits.some((h) => h.id === state.statsHabit)) {
    state.statsHabit = habits[0].id;
  }
  const habit = habits.find((h) => h.id === state.statsHabit);

  const from = state.period === "all" ? habit.start_date : shiftDate(todayStr(), -(state.period - 1));
  let stats;
  try {
    stats = await api(
      "GET",
      `/api/v1/stats?habit_id=${habit.id}&from=${from}&to=${todayStr()}`
    );
  } catch (e) {
    main().innerHTML = `<div class="page"><div class="empty">${esc(errorText(e))}</div></div>`;
    return;
  }

  const periods = [7, 30, 90, 365, "all"];
  const periodBar = periods
    .map(
      (p) =>
        `<button data-period="${p}" class="${String(state.period) === String(p) ? "active" : ""}">${esc(
          t(p === "all" ? "period_all" : `period_${p}`)
        )}</button>`
    )
    .join("");

  const habitOptions = habits
    .map(
      (h) =>
        `<option value="${h.id}" ${h.id === state.statsHabit ? "selected" : ""}>${esc(h.name)}${h.archived_at ? " ⛁" : ""}</option>`
    )
    .join("");

  const streakUnit = stats.streak.unit === "weeks" ? "streak_weeks" : "streak_days";
  const currentStreak = stats.streak.current
    ? `${stats.streak.current} <small>${esc(plural(stats.streak.current, t(streakUnit)))}</small>`
    : "0";
  const bestStreak = stats.streak.best
    ? `${stats.streak.best} <small>${esc(plural(stats.streak.best, t(streakUnit)))}</small>`
    : "0";
  const completion = stats.rate.has ? `${stats.rate.percent}<small>%</small>` : "—";

  const cells = stats.heatmap.cells
    .map(
      (c) =>
        `<div class="hm-cell ${c.state}" title="${esc(c.date)}${c.state === "future" ? "" : " · " + esc(t("status_" + c.state))}"></div>`
    )
    .join("");

  main().innerHTML = `<div class="page">
    ${pageHead(t("stats_title"), `<select class="select" id="stats-habit" style="min-width:180px">${habitOptions}</select>`)}
    <div class="seg" id="period-bar">${periodBar}</div>
    <div style="height:16px"></div>
    <div class="stat-grid">
      <div class="stat-card"><div class="stat-value">${currentStreak}</div><div class="stat-label">${esc(t("stat_current"))}</div></div>
      <div class="stat-card"><div class="stat-value">${bestStreak}</div><div class="stat-label">${esc(t("stat_best"))}</div></div>
      <div class="stat-card"><div class="stat-value">${completion}</div><div class="stat-label">${esc(t("stat_completion"))}</div></div>
    </div>
    <div class="heatmap-card">
      <div class="heatmap-head"><h3>${esc(t("heatmap_title"))}</h3></div>
      <div class="heatmap-wrap">
        <div class="hm-days">${t("weekdays_short").map((d) => `<span>${esc(d)}</span>`).join("")}</div>
        <div class="heatmap">${cells}</div>
      </div>
      <div class="hm-legend">
        <span class="item"><span class="sw" style="background:var(--accent)"></span>${esc(t("status_done"))}</span>
        <span class="item"><span class="sw" style="background:color-mix(in srgb, var(--skip) 55%, transparent)"></span>${esc(t("status_skipped"))}</span>
        <span class="item"><span class="sw" style="background:color-mix(in srgb, var(--miss) 28%, transparent)"></span>${esc(t("status_miss"))}</span>
      </div>
    </div>
    ${
      habit.schedule.kind === "weekly_count"
        ? `<p style="color:var(--muted);font-size:13px">${esc(t("schedule_weekly_count"))} · ${habit.schedule.times}×</p>`
        : ""
    }
  </div>`;

  main().querySelector("#stats-habit").addEventListener("change", (ev) => {
    state.statsHabit = Number(ev.target.value);
    renderStats();
  });
  main().querySelectorAll("#period-bar button").forEach((b) =>
    b.addEventListener("click", () => {
      state.period = b.dataset.period === "all" ? "all" : Number(b.dataset.period);
      renderStats();
    })
  );
}

/* ---------- quitting (bad habits with a timer) ---------- */

async function renderQuits() {
  main().innerHTML = `<div class="page"><div class="empty">…</div></div>`;
  let data;
  try {
    data = await api("GET", "/api/v1/quits?archived=1");
  } catch (e) {
    main().innerHTML = `<div class="page"><div class="empty">${esc(errorText(e))}</div></div>`;
    return;
  }
  state._quits = data.quits;
  const active = data.quits.filter((q) => !q.archived_at);
  const archived = data.quits.filter((q) => q.archived_at);

  const card = (q, isArchived) => {
    const start = Math.floor(parseIsoMs(q.started_at) / 1000);
    const attempts = `${q.attempts} ${plural(q.attempts, t("unit_attempts"))}`;
    return `<div class="quit-card ${isArchived ? "archived" : ""}" style="--accent:${esc(q.color)}" data-id="${q.id}">
      <div class="quit-icon" style="color:${esc(q.color)}">${icon(q.icon)}</div>
      <div class="quit-body">
        <div class="quit-head">
          <span class="quit-name">${esc(q.name)}</span>
          <span class="chip">${esc(attempts)}</span>
        </div>
        <div class="quit-timer" data-quit-start="${start}" data-quit-best="${q.best_seconds}">${formatDuration(
      Date.now() / 1000 - start
    )}</div>
        <div class="quit-sub">${esc(t("since", { date: fmtDateTime(q.started_at) }))} · ${esc(
      t("best_label")
    )}: <span class="quit-best">${formatDuration(q.best_seconds)}</span></div>
        ${q.description ? `<div class="quit-desc">${esc(q.description)}</div>` : ""}
      </div>
      <div class="quit-actions">
        <button class="btn-icon" data-action="edit" data-id="${q.id}" title="${esc(t("edit"))}" aria-label="${esc(t("edit"))}">${noteSvg()}</button>
        ${
          isArchived
            ? `<button class="btn-icon" data-action="unarchive" data-id="${q.id}" title="${esc(t("unarchive"))}" aria-label="${esc(t("unarchive"))}">${restoreSvg()}</button>
               <button class="btn-icon" data-action="delete" data-id="${q.id}" title="${esc(t("delete"))}" aria-label="${esc(t("delete"))}">${trashSvg()}</button>`
            : `<button class="btn-icon" data-action="reset" data-id="${q.id}" title="${esc(t("reset"))}" aria-label="${esc(t("reset"))}">${resetSvg()}</button>
               <button class="btn-icon" data-action="archive" data-id="${q.id}" title="${esc(t("archive"))}" aria-label="${esc(t("archive"))}">${archiveSvg()}</button>`
        }
      </div>
    </div>`;
  };

  main().innerHTML = `<div class="page">
    ${pageHead(t("quits_title"), `<button class="btn btn-primary" data-action="new">＋ ${esc(t("new_quit"))}</button>`)}
    ${active.length ? `<div class="quit-list">${active.map((q) => card(q, false)).join("")}</div>` : `<div class="empty">${esc(t("quits_empty"))}<div style="margin-top:14px"><button class="btn btn-primary" data-action="new">${esc(t("new_quit"))}</button></div></div>`}
    ${archived.length ? `<div class="section-label">${esc(t("habits_archived"))}</div><div class="quit-list">${archived.map((q) => card(q, true)).join("")}</div>` : ""}
  </div>`;

  main().querySelectorAll("[data-action]").forEach((el) => el.addEventListener("click", onQuitAction));
  startTicker();
}

function resetSvg() {
  return `<svg viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M20 12a8 8 0 1 1-2.3-5.6"/><path d="M20 4v5h-5"/></svg>`;
}

async function onQuitAction(ev) {
  const btn = ev.currentTarget;
  const id = Number(btn.dataset.id);
  const quit = (state._quits || []).find((q) => q.id === id);
  try {
    if (btn.dataset.action === "new") return openQuitModal(null);
    if (btn.dataset.action === "edit") return openQuitModal(quit);
    if (btn.dataset.action === "reset") {
      if (!confirm(t("reset_confirm"))) return;
      await api("POST", `/api/v1/quits/${id}/reset`, {});
      toast(t("toast_saved"));
      renderQuits();
      return;
    }
    if (btn.dataset.action === "archive") {
      await api("POST", `/api/v1/quits/${id}/archive`, {});
      renderQuits();
      return;
    }
    if (btn.dataset.action === "unarchive") {
      await api("POST", `/api/v1/quits/${id}/unarchive`, {});
      renderQuits();
      return;
    }
    if (btn.dataset.action === "delete") {
      if (!confirm(t("delete_confirm"))) return;
      await api("DELETE", `/api/v1/quits/${id}`);
      toast(t("toast_deleted"));
      renderQuits();
    }
  } catch (e) {
    toast(errorText(e), true);
  }
}

function openQuitModal(quit) {
  const isEdit = Boolean(quit);
  const draft = {
    color: quit ? quit.color : "#ef4444",
    icon: quit ? quit.icon : "no-smoke",
  };
  const colorSwatches = COLORS.map(
    (c) => `<button type="button" class="swatch ${c === draft.color ? "sel" : ""}" data-color="${c}"
      style="background:${c}" aria-label="${c}"></button>`
  ).join("");
  const iconOptions = Object.keys(ICONS)
    .map(
      (k) => `<button type="button" class="icon-opt ${k === draft.icon ? "sel" : ""}" data-icon="${k}"
        title="${k}">${ICONS[k]}</button>`
    )
    .join("");

  const root = document.getElementById("modal-root");
  root.innerHTML = `<div class="modal-backdrop" data-close>
    <div class="modal" role="dialog" aria-modal="true">
      <div class="modal-head">
        <h2>${esc(isEdit ? t("form_title_edit_quit") : t("form_title_new_quit"))}</h2>
        <button class="btn-icon" data-close aria-label="${esc(t("close"))}">✕</button>
      </div>
      <form id="quit-form" novalidate>
        <div class="field">
          <label for="q-name">${esc(t("form_name"))}</label>
          <input id="q-name" name="name" class="input" maxlength="80" autocomplete="off"
            placeholder="${esc(t("quit_name_placeholder"))}" value="${esc(quit ? quit.name : "")}">
        </div>
        <div class="field">
          <label for="q-desc">${esc(t("form_description"))}</label>
          <textarea id="q-desc" name="description" class="textarea" maxlength="2000">${esc(
            quit ? quit.description : ""
          )}</textarea>
        </div>
        <div class="field">
          <label for="q-start">${esc(t("form_started_at"))}</label>
          <input id="q-start" name="started_at" type="datetime-local" class="input"
            value="${esc(toLocalInput(quit ? quit.started_at : new Date().toISOString()))}">
        </div>
        <div class="field">
          <span class="field-label">${esc(t("form_color"))}</span>
          <div class="swatches">${colorSwatches}</div>
        </div>
        <div class="field">
          <span class="field-label">${esc(t("form_icon"))}</span>
          <div class="icon-grid">${iconOptions}</div>
        </div>
        <div class="form-error" id="quit-error"></div>
        <div class="modal-actions">
          ${isEdit ? `<button type="button" class="btn btn-danger danger" id="q-delete">${esc(t("delete"))}</button>` : ""}
          <button type="button" class="btn" data-close>${esc(t("cancel"))}</button>
          <button type="submit" class="btn btn-primary">${esc(t("save"))}</button>
        </div>
      </form>
    </div>
  </div>`;

  root.querySelectorAll("[data-close]").forEach((el) => {
    el.addEventListener("click", (ev) => {
      if (ev.target === el) closeModal();
    });
  });
  root.querySelectorAll(".swatch").forEach((el) =>
    el.addEventListener("click", () => {
      draft.color = el.dataset.color;
      root.querySelectorAll(".swatch").forEach((s) => s.classList.toggle("sel", s === el));
    })
  );
  root.querySelectorAll(".icon-opt").forEach((el) =>
    el.addEventListener("click", () => {
      draft.icon = el.dataset.icon;
      root.querySelectorAll(".icon-opt").forEach((s) => s.classList.toggle("sel", s === el));
    })
  );

  const errorEl = root.querySelector("#quit-error");
  const form = root.querySelector("#quit-form");
  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const name = root.querySelector("#q-name").value.trim();
    if (!name) {
      errorEl.textContent = t("form_name_required");
      return;
    }
    const startedLocal = root.querySelector("#q-start").value;
    const body = {
      name,
      description: root.querySelector("#q-desc").value,
      color: draft.color,
      icon: draft.icon,
      started_at: new Date(startedLocal).toISOString(),
    };
    try {
      if (isEdit) await api("PATCH", `/api/v1/quits/${quit.id}`, body);
      else await api("POST", "/api/v1/quits", body);
      closeModal();
      toast(t("toast_saved"));
      renderQuits();
    } catch (e) {
      errorEl.textContent = errorText(e);
    }
  });

  if (isEdit) {
    root.querySelector("#q-delete").addEventListener("click", async () => {
      if (!confirm(t("delete_confirm"))) return;
      try {
        await api("DELETE", `/api/v1/quits/${quit.id}`);
        closeModal();
        toast(t("toast_deleted"));
        renderQuits();
      } catch (e) {
        errorEl.textContent = errorText(e);
      }
    });
  }

  const nameInput = root.querySelector("#q-name");
  nameInput.focus();
  nameInput.setSelectionRange(nameInput.value.length, nameInput.value.length);
  root.querySelector(".modal").addEventListener("keydown", (ev) => {
    if (ev.key === "Escape") closeModal();
  });
}

/* ---------- settings ---------- */

async function renderSettings() {
  const langOptions = LANGS.map(
    ([code, name]) => `<option value="${code}" ${code === state.lang ? "selected" : ""}>${esc(name)}</option>`
  ).join("");

  const themeOptions = [
    ["system", "theme_system"],
    ["light", "theme_light"],
    ["dark", "theme_dark"],
  ]
    .map(([v, k]) => `<option value="${v}" ${v === state.theme ? "selected" : ""}>${esc(t(k))}</option>`)
    .join("");

  main().innerHTML = `<div class="page">
    ${pageHead(t("settings_title"), "")}
    <div class="settings-card">
      <div class="setting-row">
        <span class="setting-label">${esc(t("settings_language"))}</span>
        <select class="select" id="set-lang">${langOptions}</select>
      </div>
      <div class="setting-row">
        <span class="setting-label">${esc(t("settings_theme"))}</span>
        <select class="select" id="set-theme">${themeOptions}</select>
      </div>
      <div class="setting-row">
        <span class="setting-label">${esc(t("settings_data"))}</span>
        <span class="setting-value"><code>${esc(state.health.data_dir || "—")}</code></span>
      </div>
      <div class="setting-row">
        <span class="setting-label">${esc(t("settings_export"))}</span>
        <span class="setting-value"><a class="btn" href="/api/v1/export">${esc(t("export"))}</a></span>
      </div>
      <div class="setting-row">
        <span class="setting-label">${esc(t("settings_version"))}</span>
        <span class="setting-value">${esc(state.health.version || "—")}</span>
      </div>
    </div>
  </div>`;

  main().querySelector("#set-lang").addEventListener("change", (ev) => setLang(ev.target.value));
  main().querySelector("#set-theme").addEventListener("change", (ev) => {
    applyTheme(ev.target.value);
    renderSettings();
  });
  document.getElementById("app-version").textContent = state.health.version
    ? `v${state.health.version}`
    : "";
}

/* ---------- routing ---------- */

function route() {
  const hash = location.hash.replace(/^#\/?/, "");
  const view = ["today", "habits", "quits", "stats", "settings"].includes(hash) ? hash : "today";
  state.view = view;
  updateNav();
  render();
}

window.addEventListener("hashchange", route);

document.addEventListener("keydown", (ev) => {
  const tag = (ev.target && ev.target.tagName) || "";
  if (["INPUT", "TEXTAREA", "SELECT"].includes(tag)) return;
  if (document.querySelector(".modal-backdrop")) return;
  if (state.view === "today") {
    if (ev.key === "ArrowLeft") {
      state.date = shiftDate(state.date, -1);
      renderToday();
    } else if (ev.key === "ArrowRight" && state.date < todayStr()) {
      state.date = shiftDate(state.date, 1);
      renderToday();
    } else if (ev.key === "t") {
      state.date = todayStr();
      renderToday();
    }
  }
});

/* ---------- boot ---------- */

async function boot() {
  const params = new URLSearchParams(location.search);
  const savedLang = localStorage.getItem("lang");
  const nav = (navigator.language || "en").toLowerCase();
  const guess =
    params.get("lang") ||
    savedLang ||
    (nav.startsWith("uk") ? "uk" : nav.startsWith("ru") ? "ru" : "en");
  applyTheme(params.get("theme") || localStorage.getItem("theme") || "system");
  state.fallback = await loadDict("en");
  await setLang(guess, false);

  if (!location.hash) location.hash = "#/today";
  route();

  try {
    state.health = await api("GET", "/api/v1/health");
  } catch {
    /* offline: keep placeholders */
  }
  if (state.view === "settings") renderSettings();
  const versionEl = document.getElementById("app-version");
  if (versionEl) versionEl.textContent = state.health.version ? `v${state.health.version}` : "";
}

boot();
