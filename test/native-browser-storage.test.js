'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

const repoRoot = path.resolve(__dirname, '..');

function extractFunctionSource(filePath, functionName) {
  const content = fs.readFileSync(filePath, 'utf8');
  const signature = `function ${functionName}(`;
  const start = content.indexOf(signature);
  assert.notEqual(start, -1, `${functionName} not found in ${filePath}`);

  const bodyStart = content.indexOf('{', start);
  assert.notEqual(bodyStart, -1, `${functionName} body not found in ${filePath}`);

  let depth = 0;
  let inSingle = false;
  let inDouble = false;
  let inTemplate = false;
  let escaping = false;

  for (let index = bodyStart; index < content.length; index += 1) {
    const char = content[index];
    const previous = index > 0 ? content[index - 1] : '';

    if (escaping) {
      escaping = false;
      continue;
    }

    if ((inSingle || inDouble || inTemplate) && char === '\\') {
      escaping = true;
      continue;
    }

    if (!inDouble && !inTemplate && char === '\'' && previous !== '\\') {
      inSingle = !inSingle;
      continue;
    }
    if (!inSingle && !inTemplate && char === '"' && previous !== '\\') {
      inDouble = !inDouble;
      continue;
    }
    if (!inSingle && !inDouble && char === '`' && previous !== '\\') {
      inTemplate = !inTemplate;
      continue;
    }

    if (inSingle || inDouble || inTemplate) continue;

    if (char === '{') depth += 1;
    if (char === '}') {
      depth -= 1;
      if (depth === 0) return content.slice(start, index + 1);
    }
  }

  throw new Error(`Unable to extract ${functionName} from ${filePath}`);
}

function createLocalStorage(initialEntries = {}) {
  const store = new Map(Object.entries(initialEntries));
  return {
    getItem(key) {
      return store.has(key) ? store.get(key) : null;
    },
    setItem(key, value) {
      store.set(key, String(value));
    },
    removeItem(key) {
      store.delete(key);
    },
    dump() {
      return Object.fromEntries(store.entries());
    }
  };
}

function createElement(initial = {}) {
  return {
    value: '',
    checked: false,
    innerHTML: '',
    style: {},
    classList: {
      add() {},
      remove() {},
      toggle() {}
    },
    ...initial
  };
}

function createUiSettingsContext(initialLocalStorage = {}) {
  const quietConsole = { log() {}, warn() {}, error() {} };
  const elements = {
    lserr: createElement({ style: { display: 'none' } }),
    lssuc: createElement({ style: { display: 'none' } }),
    dm: createElement({ checked: false }),
    theme_base: createElement({ value: 'dark' }),
    gen: createElement(),
    idonthateyou: createElement({ style: { display: 'none' } })
  };

  const form = {
    DS: createElement({ value: 'WLED Native' }),
    SU: createElement({ checked: false }),
    submitCalled: false,
    submit() {
      this.submitCalled = true;
    }
  };

  const agiElements = [];
  const localStorage = createLocalStorage(initialLocalStorage);
  const generated = [];
  const toasts = [];
  const context = {
    d: {
      Sf: form,
      querySelectorAll(selector) {
        if (selector === '.agi') return agiElements;
        return [];
      }
    },
    document: null,
    localStorage,
    sett: undefined,
    initial_ds: '',
    initial_su: false,
    loc: false,
    oldUrl: '',
    l: {},
    getLoc() {},
    getURL(value) {
      return value;
    },
    loadJS(_url, _async, _preGetV, postGetV) {
      if (typeof context.GetV === 'function') context.GetV();
      if (postGetV) postGetV();
    },
    gId(id) {
      return elements[id];
    },
    genForm(value) {
      generated.push(JSON.parse(JSON.stringify(value)));
      if (Object.prototype.hasOwnProperty.call(value, 'theme_base')) elements.theme_base.value = value.theme_base;
    },
    set(key, target, value) {
      target[key] = value;
    },
    showToast(message) {
      toasts.push(message);
    },
    toggle() {},
    console: quietConsole
  };

  context.document = context.d;

  return { context, elements, form, agiElements, localStorage, generated, toasts };
}

function createLedSettingsContext(initialLocalStorage = {}) {
  const quietConsole = { log() {}, warn() {}, error() {} };
  const localStorage = createLocalStorage(initialLocalStorage);
  const form = {
    AS: createElement({ checked: false }),
    addEventListener() {}
  };

  const context = {
    d: {
      Sf: form
    },
    document: null,
    localStorage,
    loc: false,
    pinDropdowns: [],
    getLoc() {},
    getURL(value) {
      return value;
    },
    fetch() {
      return Promise.resolve({ ok: true, json: async () => ({}) });
    },
    loadJS(_url, _async, preGetV, postGetV) {
      if (preGetV) preGetV();
      if (typeof context.GetV === 'function') context.GetV();
      if (postGetV) postGetV();
    },
    checkSi() {},
    setABL() {},
    fetchPinInfo() {},
    trySubmit() {},
    console: quietConsole
  };

  context.document = context.d;

  return { context, form, localStorage };
}

test('UI settings page stores and reloads browser-local UI config', () => {
  const scriptPath = path.join(repoRoot, 'wled00/data/settings_ui.htm');
  const sources = [
    extractFunctionSource(scriptPath, 'GetLS'),
    extractFunctionSource(scriptPath, 'SetLS'),
    extractFunctionSource(scriptPath, 'Save'),
    extractFunctionSource(scriptPath, 'cLS')
  ].join('\n');

  const { context, elements, form, agiElements, localStorage, generated, toasts } = createUiSettingsContext({
    wledUiCfg: JSON.stringify({ theme_base: 'light', lang: 'en' }),
    wledP: '{"foo":1}',
    wledPmt: 'etag',
    wledPalx: '{"pal":1}'
  });

  agiElements.push(
    createElement({ id: 'theme_base', value: 'dark', classList: { contains: () => false } }),
    createElement({ id: 'lang', value: 'de', classList: { contains: () => false } })
  );

  vm.runInNewContext(sources, context);

  context.GetLS();
  assert.deepEqual(generated[0], { theme_base: 'light', lang: 'en' });
  assert.equal(elements.dm.checked, true);

  context.SetLS();
  assert.deepEqual(JSON.parse(localStorage.getItem('wledUiCfg')), { theme_base: 'dark', lang: 'de' });
  assert.equal(elements.lssuc.style.display, 'inline');

  form.submitCalled = false;
  context.initial_ds = form.DS.value;
  context.initial_su = form.SU.checked;
  context.Save();
  assert.equal(form.submitCalled, false);
  assert.deepEqual(JSON.parse(localStorage.getItem('wledUiCfg')), { theme_base: 'dark', lang: 'de' });

  form.DS.value = 'Desk Controller';
  context.Save();
  assert.equal(form.submitCalled, true);

  context.cLS();
  assert.equal(localStorage.getItem('wledP'), null);
  assert.equal(localStorage.getItem('wledPmt'), null);
  assert.equal(localStorage.getItem('wledPalx'), null);
  assert.deepEqual(toasts, ['Cleared.']);
});

test('LED settings page restores advanced-toggle state from browser-local storage', async () => {
  const scriptPath = path.join(repoRoot, 'wled00/data/settings_leds.htm');
  const source = extractFunctionSource(scriptPath, 'S');
  const { context, form } = createLedSettingsContext({ ASc: 'true' });

  vm.runInNewContext(source, context);
  await context.S();
  assert.equal(form.AS.checked, true);

  form.AS.checked = false;
  context.localStorage.setItem('ASc', 'false');
  await context.S();
  assert.equal(form.AS.checked, false);
});
