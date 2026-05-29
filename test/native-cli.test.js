'use strict';

const assert = require('node:assert/strict');
const { test } = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const childProcess = require('node:child_process');

const repoRoot = path.resolve(__dirname, '..');
const nativeRunScript = path.join(repoRoot, 'scripts', 'native-run.sh');

function runNativeCommand(args) {
  return childProcess.spawnSync(nativeRunScript, args, {
    cwd: repoRoot,
    encoding: 'utf8'
  });
}

async function startNativeServer(configDir, extraArgs = []) {
  const child = childProcess.spawn(nativeRunScript, ['--config-dir', configDir, '--host', '127.0.0.1', '--port', '0', ...extraArgs], {
    cwd: repoRoot,
    stdio: ['ignore', 'pipe', 'pipe']
  });

  let stdout = '';
  let stderr = '';

  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  child.stdout.on('data', (chunk) => {
    stdout += chunk;
  });
  child.stderr.on('data', (chunk) => {
    stderr += chunk;
  });

  const listeningUrl = await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      reject(new Error(`Timed out waiting for native server startup.\nstdout:\n${stdout}\nstderr:\n${stderr}`));
    }, 5000);

    function checkOutput() {
      const match = stdout.match(/^Listening on: (http:\/\/.+)$/m);
      if (match) {
        clearTimeout(timeout);
        resolve(match[1].trim());
      }
    }

    child.stdout.on('data', checkOutput);
    child.on('exit', (code, signal) => {
      clearTimeout(timeout);
      reject(new Error(`Native server exited before startup (code=${code}, signal=${signal}).\nstdout:\n${stdout}\nstderr:\n${stderr}`));
    });
    checkOutput();
  });

  return {
    child,
    listeningUrl,
    getStdout: () => stdout,
    getStderr: () => stderr,
    async stop() {
      if (child.exitCode !== null) return;
      child.kill('SIGTERM');
      await new Promise((resolve) => child.once('exit', resolve));
    }
  };
}

function extractField(output, label) {
  const match = output.match(new RegExp(`^${label}: (.+)$`, 'm'));
  assert.ok(match, `Expected ${label} in output:\n${output}`);
  return match[1].trim();
}

function readJsonFile(filePath) {
  return JSON.parse(fs.readFileSync(filePath, 'utf8'));
}

function parseDumpedJson(output) {
  let jsonText = output.slice(output.indexOf('JSON target:'));
  jsonText = jsonText.split('\n').slice(1).join('\n').trim();
  return JSON.parse(jsonText);
}

function parseDumpedRoute(output) {
  let routeText = output.slice(output.indexOf('Route target:'));
  routeText = routeText.split('\n').slice(1).join('\n').trim();
  return routeText;
}

function parseAppliedJson(output) {
  let jsonText = output.slice(output.indexOf('Applied JSON:'));
  jsonText = jsonText.split('\n').slice(1).join('\n').trim();
  return JSON.parse(jsonText);
}

function waitForSocketMessage(socket, predicate, errorMessage) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      socket.removeEventListener('message', onMessage);
      reject(new Error(errorMessage));
    }, 5000);

    function onMessage(event) {
      if (!predicate(event.data)) return;
      clearTimeout(timeout);
      socket.removeEventListener('message', onMessage);
      resolve(event.data);
    }

    socket.addEventListener('message', onMessage);
  });
}

function blendColor(color1, color2, blend) {
  const twoChannelMask = 0x00FF00FF;
  const rb1 = color1 & twoChannelMask;
  const wg1 = (color1 >>> 8) & twoChannelMask;
  const rb2 = color2 & twoChannelMask;
  const wg2 = (color2 >>> 8) & twoChannelMask;
  const rb3 = ((((rb1 << 8) | rb2) + (rb2 * blend) - (rb1 * blend)) >>> 8) & twoChannelMask;
  const wg3 = ((((wg1 << 8) | wg2) + (wg2 * blend) - (wg1 * blend))) & ~twoChannelMask;
  return (rb3 | wg3) >>> 0;
}

function fadeColor(color, amount, video) {
  if (color === 0 || amount === 0) return 0;
  if (amount === 255) return color >>> 0;

  const twoChannelMask = 0x00FF00FF;
  const rb = color & twoChannelMask;
  const wg = (color >>> 8) & twoChannelMask;

  let rbScaled;
  let wgScaled;

  if (video) {
    rbScaled = ((rb * amount + 0x007F007F) >>> 8) & twoChannelMask;
    wgScaled = (wg * amount + 0x007F007F) & ~twoChannelMask;
    const r = (rb >>> 16) & 0xFF;
    const g = wg & 0xFF;
    const b = rb & 0xFF;
    const w = (wg >>> 16) & 0xFF;
    const maxRgb = Math.max(r, g, b);
    const maxc = (Math.max(maxRgb, w) >>> 2) + 1;
    rbScaled |= r > maxc ? 0x00010000 : 0;
    wgScaled |= g > maxc ? 0x00000100 : 0;
    rbScaled |= b > maxc ? 0x00000001 : 0;
    wgScaled |= w ? 0x01000000 : 0;
  } else {
    rbScaled = ((rb * (amount + 1)) >>> 8) & twoChannelMask;
    wgScaled = (wg * (amount + 1)) & ~twoChannelMask;
  }

  return (rbScaled | wgScaled) >>> 0;
}

function addColor(color1, color2, preserveColorRatio) {
  if (color1 === 0) return color2 >>> 0;
  if (color2 === 0) return color1 >>> 0;

  const twoChannelMask = 0x00FF00FF;
  let rb = (color1 & twoChannelMask) + (color2 & twoChannelMask);
  let wg = (((color1 >>> 8) & twoChannelMask) + ((color2 >>> 8) & twoChannelMask)) >>> 0;

  if (preserveColorRatio) {
    const overflow = (rb | wg) & 0x01000100;
    if (overflow) {
      const r = rb >>> 16;
      const b = rb & 0xFFFF;
      const w = wg >>> 16;
      const g = wg & 0xFFFF;
      let maxValue = Math.max(r, g, b, w);
      const scale = ((255 << 8) / maxValue) >>> 0;
      rb = (((rb * scale) >>> 8) & twoChannelMask) >>> 0;
      wg = ((wg * scale) & (~twoChannelMask >>> 0)) >>> 0;
    } else {
      wg = (wg << 8) >>> 0;
    }
  } else {
    rb |= (((rb & 0x01000100) - ((rb >>> 8) & 0x00010001)) & 0x00FF00FF) >>> 0;
    wg |= (((wg & 0x01000100) - ((wg >>> 8) & 0x00010001)) & 0x00FF00FF) >>> 0;
    wg = (wg << 8) >>> 0;
  }

  return (rb | wg) >>> 0;
}

function formatColor(color) {
  return `0x${color.toString(16).toUpperCase().padStart(8, '0')}`;
}

function prngSequence(seed, count) {
  let current = seed & 0xFFFF;
  const output = [];
  for (let index = 0; index < count; index += 1) {
    current = ((current * 3001) + 31683) & 0xFFFF;
    current ^= current >>> 7;
    current &= 0xFFFF;
    output.push(current);
  }
  return output;
}

test('native wrapper prints WLED help text', () => {
  const result = runNativeCommand(['--help']);
  assert.equal(result.status, 0, result.stderr || result.stdout);
  assert.match(result.stdout, /Usage: .*WLED/i);
  assert.match(result.stdout, /--config-dir/);
  assert.match(result.stdout, /--exit-after-bootstrap/);
  assert.match(result.stdout, /--host/);
  assert.match(result.stdout, /--port/);
  assert.match(result.stdout, /--log-level/);
  assert.match(result.stdout, /--version/);
  assert.match(result.stdout, /--blend-color/);
  assert.match(result.stdout, /--add-color/);
  assert.match(result.stdout, /--fade-color/);
  assert.match(result.stdout, /--prng-seq/);
  assert.match(result.stdout, /--playlist-run/);
  assert.match(result.stdout, /--dump-json/);
  assert.match(result.stdout, /--apply-json/);
  assert.match(result.stdout, /--init-presets/);
  assert.match(result.stdout, /--preset-name/);
  assert.match(result.stdout, /--delete-preset/);
  assert.match(result.stdout, /--backup-config/);
  assert.match(result.stdout, /--restore-config/);
  assert.match(result.stdout, /--verify-config/);
  assert.match(result.stdout, /--reset-config/);
  assert.match(result.stdout, /--has-config-backup/);
  assert.match(result.stdout, /--verify-secrets/);
});

test('native wrapper prints WLED version', () => {
  const result = runNativeCommand(['--version']);
  assert.equal(result.status, 0, result.stderr || result.stdout);
  assert.match(result.stdout, /^WLED 17\.0\.0-dev/m);
});

test('native wrapper resolves config dir and persists an instance id', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const firstResult = runNativeCommand(['--config-dir', configDir, '--exit-after-bootstrap']);
    assert.equal(firstResult.status, 0, firstResult.stderr || firstResult.stdout);
    assert.match(firstResult.stdout, /WLED host runtime bootstrap/);
    assert.equal(extractField(firstResult.stdout, 'Config root'), configDir);

    const firstInstanceId = extractField(firstResult.stdout, 'Instance ID');
    assert.match(firstInstanceId, /^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$/);
    assert.ok(fs.existsSync(path.join(configDir, 'instance-id')));
    assert.deepEqual(readJsonFile(path.join(configDir, 'cfg.json')), {});
    assert.deepEqual(readJsonFile(path.join(configDir, 'wsec.json')), {});
    assert.deepEqual(readJsonFile(path.join(configDir, 'presets.json')), { '0': {} });
    assert.deepEqual(readJsonFile(path.join(configDir, 'tmp.json')), { '0': {} });

    const secondResult = runNativeCommand(['--config-dir', configDir, '--exit-after-bootstrap']);
    assert.equal(secondResult.status, 0, secondResult.stderr || secondResult.stdout);
    assert.equal(extractField(secondResult.stdout, 'Instance ID'), firstInstanceId);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper serves HTTP routes in default server mode', async () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  let server;
  try {
    server = await startNativeServer(configDir);

    const indexResponse = await fetch(`${server.listeningUrl}/`, { signal: AbortSignal.timeout(5000) });
    assert.equal(indexResponse.status, 200);
    assert.match(await indexResponse.text(), /WLED/i);

    const infoResponse = await fetch(`${server.listeningUrl}/json/info`, { signal: AbortSignal.timeout(5000) });
    assert.equal(infoResponse.status, 200);
    const info = await infoResponse.json();
    assert.equal(info.brand, 'WLED');
    assert.equal(info.product, 'WLED');
    assert.equal(info.arch, 'native');
    assert.equal(info.mac, fs.readFileSync(path.join(configDir, 'instance-id'), 'utf8').trim());

    const combinedResponse = await fetch(`${server.listeningUrl}/json`, { signal: AbortSignal.timeout(5000) });
    assert.equal(combinedResponse.status, 200);
    const combined = await combinedResponse.json();
    assert.equal(combined.info.mac, info.mac);
    assert.deepEqual(combined.info.um, [9]);
    assert.equal(combined.state.on, true);
    assert.equal(combined.state.seg[0].fx, 0);
    assert.equal(combined.state.Autosave.enabled, true);

    const settingsResponse = await fetch(`${server.listeningUrl}/settings`, { signal: AbortSignal.timeout(5000) });
    assert.equal(settingsResponse.status, 200);
    assert.match(await settingsResponse.text(), /WLED Settings/);

    const wifiSettingsResponse = await fetch(`${server.listeningUrl}/settings/wifi`, { signal: AbortSignal.timeout(5000) });
    assert.equal(wifiSettingsResponse.status, 200);
    assert.match(await wifiSettingsResponse.text(), /WiFi Settings/);

    const securitySettingsResponse = await fetch(`${server.listeningUrl}/settings/sec`, { signal: AbortSignal.timeout(5000) });
    assert.equal(securitySettingsResponse.status, 200);
    assert.match(await securitySettingsResponse.text(), /Security & Update Setup/);

    const settingsScriptResponse = await fetch(`${server.listeningUrl}/settings/s.js?p=1`, { signal: AbortSignal.timeout(5000) });
    assert.equal(settingsScriptResponse.status, 200);
    assert.match(settingsScriptResponse.headers.get('content-type') || '', /javascript/);
    assert.match(await settingsScriptResponse.text(), /function GetV\(\)/);

    const networkResponse = await fetch(`${server.listeningUrl}/json/net`, { signal: AbortSignal.timeout(5000) });
    assert.equal(networkResponse.status, 200);
    const networkPayload = await networkResponse.json();
    assert.equal(Array.isArray(networkPayload.networks), true);

    const pinsResponse = await fetch(`${server.listeningUrl}/json/pins`, { signal: AbortSignal.timeout(5000) });
    assert.equal(pinsResponse.status, 200);
    const pinsPayload = await pinsResponse.json();
    assert.equal(Array.isArray(pinsPayload.pins), true);
    assert.ok(pinsPayload.pins.length >= 8);

    const presetsResponse = await fetch(`${server.listeningUrl}/presets.json`, { signal: AbortSignal.timeout(5000) });
    assert.equal(presetsResponse.status, 200);
    assert.deepEqual(await presetsResponse.json(), { '0': {} });

    const cfgResponse = await fetch(`${server.listeningUrl}/json/cfg`, { signal: AbortSignal.timeout(5000) });
    assert.equal(cfgResponse.status, 200);
    const cfg = await cfgResponse.json();
    assert.equal(cfg.um.Autosave.enabled, true);
  } finally {
    if (server) await server.stop();
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native autosave usermod saves a preset after state changes settle', async () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  let server;
  try {
    fs.writeFileSync(path.join(configDir, 'instance-id'), 'AA:BB:CC:DD:EE:01\n');
    fs.writeFileSync(
      path.join(configDir, 'cfg.json'),
      JSON.stringify({
        um: {
          Autosave: {
            enabled: true,
            autoSaveAfterSec: 1,
            autoSavePreset: 249,
            autoSaveApplyOnBoot: false,
            autoSaveIgnorePresets: false
          }
        }
      })
    );

    server = await startNativeServer(configDir);

    const saveResponse = await fetch(`${server.listeningUrl}/json/state`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ bri: 66, seg: { fx: 3, pal: 5 } }),
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(saveResponse.status, 200);

    await new Promise((resolve) => setTimeout(resolve, 1300));

    const presetsResponse = await fetch(`${server.listeningUrl}/presets.json`, { signal: AbortSignal.timeout(5000) });
    assert.equal(presetsResponse.status, 200);
    const presets = await presetsResponse.json();
    assert.equal(presets['249'].bri, 66);
    assert.equal(presets['249'].seg[0].fx, 3);
    assert.equal(presets['249'].seg[0].pal, 5);
  } finally {
    if (server) await server.stop();
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper accepts HTTP and WebSocket state updates in server mode', async () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  let server;
  let socket;
  try {
    server = await startNativeServer(configDir);
    socket = new WebSocket(server.listeningUrl.replace('http://', 'ws://') + '/ws');

    const messages = [];
    await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('Timed out waiting for WebSocket open')), 5000);
      socket.addEventListener('message', (event) => {
        messages.push(JSON.parse(event.data));
      }, { once: true });
      socket.addEventListener('open', () => {
        clearTimeout(timeout);
        resolve();
      }, { once: true });
      socket.addEventListener('error', (event) => {
        clearTimeout(timeout);
        reject(event.error || new Error('WebSocket error'));
      }, { once: true });
    });

    const postResponse = await fetch(`${server.listeningUrl}/json/state`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ on: false, bri: 42 }),
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(postResponse.status, 200);

    const postedState = await postResponse.json();
    assert.equal(postedState.state.on, false);
    assert.equal(postedState.state.bri, 42);

    const wsMessage = await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error(`Timed out waiting for WebSocket update. Messages: ${JSON.stringify(messages)}`)), 5000);
      socket.addEventListener('message', (event) => {
        clearTimeout(timeout);
        resolve(JSON.parse(event.data));
      }, { once: true });
    });
    assert.equal(wsMessage.state.on, false);
    assert.equal(wsMessage.state.bri, 42);

    socket.send(JSON.stringify({ on: true, bri: 99 }));
    const echoedMessage = await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('Timed out waiting for WebSocket echo update')), 5000);
      socket.addEventListener('message', (event) => {
        clearTimeout(timeout);
        resolve(JSON.parse(event.data));
      }, { once: true });
    });
    assert.equal(echoedMessage.state.on, true);
    assert.equal(echoedMessage.state.bri, 99);

    const finalStateResponse = await fetch(`${server.listeningUrl}/json/state`, { signal: AbortSignal.timeout(5000) });
    assert.equal(finalStateResponse.status, 200);
    const finalState = await finalStateResponse.json();
    assert.equal(finalState.on, true);
    assert.equal(finalState.bri, 99);

    const colorResponse = await fetch(`${server.listeningUrl}/json/state`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ seg: { col: [[12, 34, 56], [1, 2, 3], [4, 5, 6, 7]] } }),
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(colorResponse.status, 200);
    const colorState = await colorResponse.json();
    assert.deepEqual(colorState.state.seg[0].col, [[12, 34, 56, 0], [1, 2, 3, 0], [4, 5, 6, 7]]);

    const segmentResponse = await fetch(`${server.listeningUrl}/json/state`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ seg: { fx: 7, pal: 9, sx: 111, ix: 222, bri: 77 } }),
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(segmentResponse.status, 200);
    const segmentState = await segmentResponse.json();
    assert.equal(segmentState.state.seg[0].fx, 7);
    assert.equal(segmentState.state.seg[0].pal, 9);
    assert.equal(segmentState.state.seg[0].sx, 111);
    assert.equal(segmentState.state.seg[0].ix, 222);
    assert.equal(segmentState.state.seg[0].bri, 77);

    socket.binaryType = 'arraybuffer';
    socket.send(JSON.stringify({ v: true }));
    const verboseMessage = await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('Timed out waiting for WebSocket verbose response')), 5000);
      socket.addEventListener('message', (event) => {
        clearTimeout(timeout);
        resolve(JSON.parse(event.data));
      }, { once: true });
    });
    assert.equal(verboseMessage.state.on, true);
    assert.equal(verboseMessage.state.bri, 99);

    socket.send(JSON.stringify({ lv: true }));
    const liveViewAck = await waitForSocketMessage(socket, (data) => data === '{"success":true}', 'Timed out waiting for WebSocket live-view ack');
    assert.equal(liveViewAck, '{"success":true}');

    const liveViewFrame = await waitForSocketMessage(socket, (data) => data instanceof ArrayBuffer, 'Timed out waiting for WebSocket live-view frame');
    assert.ok(liveViewFrame instanceof ArrayBuffer, `Expected ArrayBuffer live-view frame, got ${typeof liveViewFrame}`);
    const liveViewBytes = new Uint8Array(liveViewFrame);
    assert.equal(liveViewBytes[0], 'L'.charCodeAt(0));
    assert.equal(liveViewBytes[1], 1);
    assert.ok(liveViewBytes.length > 8);
    const pixelTriples = [];
    for (let index = 2; index + 2 < Math.min(liveViewBytes.length, 26); index += 3) {
      pixelTriples.push(`${liveViewBytes[index]}-${liveViewBytes[index + 1]}-${liveViewBytes[index + 2]}`);
    }
    assert.ok(new Set(pixelTriples).size > 1, `Expected animated live-view frame, got ${pixelTriples.join(', ')}`);

    socket.send('p');
    const pongMessage = await waitForSocketMessage(socket, (data) => data === 'pong', 'Timed out waiting for WebSocket pong response');
    assert.equal(pongMessage, 'pong');
  } finally {
    if (socket && socket.readyState < WebSocket.CLOSING) socket.close();
    if (server) await server.stop();
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper persists native settings posts into cfg-backed responses', async () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  let server;
  try {
    server = await startNativeServer(configDir);

    const formBody = new URLSearchParams({
      DS: 'Desk Controller',
      SU: 'on'
    });

    const saveResponse = await fetch(`${server.listeningUrl}/settings/ui`, {
      method: 'POST',
      headers: {
        'content-type': 'application/x-www-form-urlencoded'
      },
      body: formBody,
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(saveResponse.status, 200);
    assert.match(await saveResponse.text(), /saved/i);

    const cfgResponse = await fetch(`${server.listeningUrl}/json/cfg`, { signal: AbortSignal.timeout(5000) });
    assert.equal(cfgResponse.status, 200);
    const cfg = await cfgResponse.json();
    assert.equal(cfg.id.name, 'Desk Controller');
    assert.equal(cfg.ui.simplified, true);

    const settingsScriptResponse = await fetch(`${server.listeningUrl}/settings/s.js?p=3`, { signal: AbortSignal.timeout(5000) });
    assert.equal(settingsScriptResponse.status, 200);
    const settingsScript = await settingsScriptResponse.text();
    assert.match(settingsScript, /Desk Controller/);
    assert.match(settingsScript, /\.checked=true/);
  } finally {
    if (server) await server.stop();
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper round-trips implemented settings pages through cfg, settings scripts, and restart', async () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  let server;
  try {
    server = await startNativeServer(configDir);

    const posts = [
      ['/settings/wifi', new URLSearchParams({
        CM: 'desk-native',
        AS: 'Desk AP',
        AC: '11',
        D0: '1',
        D1: '1',
        D2: '1',
        D3: '1'
      })],
      ['/settings/leds', new URLSearchParams({
        LC0: '144',
        TD: '25',
        CA: '201'
      })],
      ['/settings/sync', new URLSearchParams({
        UP: '3333',
        U2: '3334',
        UR: '2',
        EP: '4048',
        EU: '77',
        DA: '12',
        XX: '5',
        PY: '101',
        DM: '3',
        ET: '900',
        WO: '-7',
        NL: 'on'
      })],
      ['/settings/time', new URLSearchParams({
        NS: 'time.example.org',
        TZ: '4',
        UO: '-28800',
        LT: '-33.86',
        LN: '-151.21'
      })],
      ['/settings/sec', new URLSearchParams({
        PIN: '4321',
        OW: 'on',
        AO: 'on'
      })]
    ];

    for (const [route, body] of posts) {
      const response = await fetch(`${server.listeningUrl}${route}`, {
        method: 'POST',
        headers: {
          'content-type': 'application/x-www-form-urlencoded'
        },
        body,
        signal: AbortSignal.timeout(5000)
      });
      assert.equal(response.status, 200, `${route} failed: ${await response.text()}`);
    }

    let cfgResponse = await fetch(`${server.listeningUrl}/json/cfg`, { signal: AbortSignal.timeout(5000) });
    assert.equal(cfgResponse.status, 200);
    let cfg = await cfgResponse.json();
    assert.equal(cfg.host.wifi.mdns, 'desk-native');
    assert.equal(cfg.host.wifi.apSsid, 'Desk AP');
    assert.equal(cfg.host.wifi.apChannel, 11);
    assert.deepEqual(cfg.host.wifi.dns, [1, 1, 1, 1]);
    assert.equal(cfg.host.led.count, 144);
    assert.equal(cfg.host.led.transition, 25);
    assert.equal(cfg.host.led.brightness, 201);
    assert.equal(cfg.host.sync.udpPort, 3333);
    assert.equal(cfg.host.sync.udpPort2, 3334);
    assert.equal(cfg.host.sync.udpRetries, 2);
    assert.equal(cfg.host.sync.realtimePort, 4048);
    assert.equal(cfg.host.sync.realtimeUniverse, 77);
    assert.equal(cfg.host.sync.dmxAddress, 12);
    assert.equal(cfg.host.sync.dmxSpacing, 5);
    assert.equal(cfg.host.sync.e131Priority, 101);
    assert.equal(cfg.host.sync.dmxMode, 3);
    assert.equal(cfg.host.sync.realtimeTimeoutMs, 900);
    assert.equal(cfg.host.sync.realtimeOffset, -7);
    assert.equal(cfg.host.sync.nodeListEnabled, true);
    assert.equal(cfg.host.sync.nodeBroadcastEnabled, false);
    assert.equal(cfg.host.time.ntpServer, 'time.example.org');
    assert.equal(cfg.host.time.timezone, 4);
    assert.equal(cfg.host.time.utcOffsetSeconds, -28800);
    assert.equal(cfg.host.time.latitude, '-33.86');
    assert.equal(cfg.host.time.longitude, '-151.21');
    assert.equal(cfg.host.security.pin, '4321');
    assert.equal(cfg.host.security.otaLock, false);
    assert.equal(cfg.host.security.wifiLock, true);
    assert.equal(cfg.host.security.arduinoOta, true);
    assert.equal(cfg.host.security.otaSameSubnet, false);

    const infoResponse = await fetch(`${server.listeningUrl}/json/info`, { signal: AbortSignal.timeout(5000) });
    assert.equal(infoResponse.status, 200);
    const info = await infoResponse.json();
    assert.equal(info.name, 'WLED Native');

    const stateResponse = await fetch(`${server.listeningUrl}/json/state`, { signal: AbortSignal.timeout(5000) });
    assert.equal(stateResponse.status, 200);
    const state = await stateResponse.json();
    assert.equal(state.bri, 201);
    assert.equal(state.transition, 25);
    assert.equal(state.seg[0].len, 144);

    const wifiScript = await (await fetch(`${server.listeningUrl}/settings/s.js?p=1`, { signal: AbortSignal.timeout(5000) })).text();
    assert.match(wifiScript, /desk-native/);
    assert.match(wifiScript, /Desk AP/);
    assert.match(wifiScript, /1"\]\.value="1"/);

    const ledsScript = await (await fetch(`${server.listeningUrl}/settings/s.js?p=2`, { signal: AbortSignal.timeout(5000) })).text();
    assert.match(ledsScript, /"LC0"\]\.value="144"/);
    assert.match(ledsScript, /"TD"\]\.value="25"/);
    assert.match(ledsScript, /"CA"\]\.value="201"/);

    const syncScript = await (await fetch(`${server.listeningUrl}/settings/s.js?p=4`, { signal: AbortSignal.timeout(5000) })).text();
    assert.match(syncScript, /"UP"\]\.value="3333"/);
    assert.match(syncScript, /"WO"\]\.value="-7"/);
    assert.match(syncScript, /"NL"\]\.checked=true/);
    assert.match(syncScript, /"NB"\]\.checked=false/);

    const timeScript = await (await fetch(`${server.listeningUrl}/settings/s.js?p=5`, { signal: AbortSignal.timeout(5000) })).text();
    assert.match(timeScript, /time\.example\.org/);
    assert.match(timeScript, /"UO"\]\.value="-28800"/);
    assert.match(timeScript, /"LT"\]\.value="-33\.86"/);
    assert.match(timeScript, /"LN"\]\.value="-151\.21"/);
    assert.match(timeScript, /updLatLon\(\)/);

    const securityScript = await (await fetch(`${server.listeningUrl}/settings/s.js?p=6`, { signal: AbortSignal.timeout(5000) })).text();
    assert.match(securityScript, /"PIN"\]\.value="4321"/);
    assert.match(securityScript, /"NO"\]\.checked=false/);
    assert.match(securityScript, /"OW"\]\.checked=true/);
    assert.match(securityScript, /"AO"\]\.checked=true/);
    assert.match(securityScript, /"SU"\]\.checked=false/);

    await server.stop();
    server = await startNativeServer(configDir);

    cfgResponse = await fetch(`${server.listeningUrl}/json/cfg`, { signal: AbortSignal.timeout(5000) });
    assert.equal(cfgResponse.status, 200);
    cfg = await cfgResponse.json();
    assert.equal(cfg.host.wifi.mdns, 'desk-native');
    assert.equal(cfg.host.led.count, 144);
    assert.equal(cfg.host.sync.realtimeOffset, -7);
    assert.equal(cfg.host.time.latitude, '-33.86');
    assert.equal(cfg.host.security.pin, '4321');

    const restartedStateResponse = await fetch(`${server.listeningUrl}/json/state`, { signal: AbortSignal.timeout(5000) });
    assert.equal(restartedStateResponse.status, 200);
    const restartedState = await restartedStateResponse.json();
    assert.equal(restartedState.bri, 201);
    assert.equal(restartedState.transition, 25);
    assert.equal(restartedState.seg[0].len, 144);
  } finally {
    if (server) await server.stop();
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper applies persisted presets through the JSON state API', async () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  let server;
  try {
    fs.writeFileSync(
      path.join(configDir, 'presets.json'),
      JSON.stringify({
        0: {},
        1: {
          n: 'Focus',
          on: false,
          bri: 42,
          transition: 11,
          seg: [{ fx: 0, pal: 0 }]
        }
      })
    );

    server = await startNativeServer(configDir);

    const presetResponse = await fetch(`${server.listeningUrl}/json/state`, {
      method: 'POST',
      headers: {
        'content-type': 'application/json'
      },
      body: JSON.stringify({ ps: 1 }),
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(presetResponse.status, 200);
    const presetState = await presetResponse.json();
    assert.equal(presetState.state.ps, 1);
    assert.equal(presetState.state.on, false);
    assert.equal(presetState.state.bri, 42);
    assert.equal(presetState.state.transition, 11);

    const stateResponse = await fetch(`${server.listeningUrl}/json/state`, { signal: AbortSignal.timeout(5000) });
    assert.equal(stateResponse.status, 200);
    const state = await stateResponse.json();
    assert.equal(state.ps, 1);
    assert.equal(state.on, false);
    assert.equal(state.bri, 42);
    assert.equal(state.transition, 11);
  } finally {
    if (server) await server.stop();
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper dumps browser-facing JSON payloads without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    fs.writeFileSync(
      path.join(configDir, 'cfg.json'),
      JSON.stringify({
        id: { name: 'Native Dump' },
        def: { ps: 4 }
      })
    );
    fs.writeFileSync(
      path.join(configDir, 'presets.json'),
      JSON.stringify({
        0: {},
        4: {
          n: 'Booted',
          on: true,
          bri: 77,
          transition: 12,
          seg: [{ fx: 2, pal: 3 }]
        }
      })
    );

    let result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'all']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    let payload = parseDumpedJson(result.stdout);
    assert.equal(payload.info.name, 'Native Dump');
    assert.equal(payload.info.leds.bootps, 4);
    assert.equal(payload.info.leds.maxseg >= 1, true);
    assert.equal(Array.isArray(payload.info.leds.seglc), true);
    assert.equal(typeof payload.info.str, 'boolean');
    assert.equal(payload.state.ps, 4);
    assert.equal(payload.state.bri, 77);
    assert.deepEqual(payload.state.udpn, { send: false, recv: false, sgrp: 0, rgrp: 0 });
    assert.ok(Array.isArray(payload.effects));
    assert.ok(payload.effects.length > 20);
    assert.ok(Array.isArray(payload.palettes));
    assert.ok(payload.palettes.length > 10);

    result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'palx:0']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    payload = parseDumpedJson(result.stdout);
    assert.equal(payload.m >= 0, true);
    assert.ok(payload.p['0']);
    assert.ok(payload.p['5']);

    result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'live']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    payload = parseDumpedJson(result.stdout);
    assert.equal(Array.isArray(payload.leds), true);
    assert.equal(payload.leds.length, 30);
    assert.match(payload.leds[0], /^[0-9A-F]{6}$/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper restores custom palette browser contracts without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    fs.writeFileSync(
      path.join(configDir, 'palette0.json'),
      JSON.stringify({
        palette: [0, 255, 0, 0, 255, 0, 0, 255]
      })
    );
    fs.writeFileSync(
      path.join(configDir, 'palette2.json'),
      JSON.stringify({
        palette: [0, '112233', 128, '445566', 255, '778899']
      })
    );

    let result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'info']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    let payload = parseDumpedJson(result.stdout);
    assert.equal(payload.cpalcount, 3);
    assert.equal(payload.cpalmax >= 3, true);

    result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'pal']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    payload = parseDumpedJson(result.stdout);
    assert.equal(Array.isArray(payload), true);
    assert.equal(payload[0], 'Default');
    assert.ok(payload.length > 10);

    result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'palx:999']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    payload = parseDumpedJson(result.stdout);
    assert.ok(payload.p['200']);
    assert.ok(payload.p['199']);
    assert.ok(payload.p['198']);
    assert.equal(payload.p['199'].length, 16);
    assert.equal(payload.p['199'].every((stop) => stop[1] === 128 && stop[2] === 128 && stop[3] === 128), true);

    result = runNativeCommand(['--config-dir', configDir, '--apply-json', '/json/state:{"rmcpal":2}']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    payload = parseAppliedJson(result.stdout);
    assert.equal(payload.info.cpalcount, 1);
    assert.equal(fs.existsSync(path.join(configDir, 'palette2.json')), false);

    result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'info']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    payload = parseDumpedJson(result.stdout);
    assert.equal(payload.cpalcount, 1);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper renders non-uniform live output for animated effects without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    let result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'effects']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    const effects = parseDumpedJson(result.stdout);
    assert.ok(Array.isArray(effects));

    const animatedEffect = effects.findIndex((name, index) => (
      index > 0 && /(rainbow|chase|scan|sweep|wipe|comet|running)/i.test(name)
    ));
    assert.ok(animatedEffect > 0, `Expected an animated effect in catalog, got: ${effects.slice(0, 20).join(', ')}`);

    fs.writeFileSync(
      path.join(configDir, 'cfg.json'),
      JSON.stringify({
        def: { ps: 9 }
      })
    );
    fs.writeFileSync(
      path.join(configDir, 'presets.json'),
      JSON.stringify({
        0: {},
        9: {
          n: 'Animated Boot',
          on: true,
          bri: 200,
          seg: [{
            fx: animatedEffect,
            pal: 9,
            sx: 180,
            ix: 200,
            col: [[200, 40, 10], [10, 80, 255], [0, 0, 0]]
          }]
        }
      })
    );

    result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'live']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    const payload = parseDumpedJson(result.stdout);
    assert.equal(Array.isArray(payload.leds), true);
    assert.equal(payload.leds.length, 30);
    assert.ok(new Set(payload.leds).size > 1, `Expected animated live output, got: ${payload.leds.slice(0, 10).join(', ')}`);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper supports legacy /edit query forms without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    fs.writeFileSync(path.join(configDir, 'alpha.json'), '{"alpha":1}\n', 'utf8');
    fs.writeFileSync(path.join(configDir, 'notes.txt'), 'hello native\n', 'utf8');

    let result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/edit?list=/']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    let payload = JSON.parse(parseDumpedRoute(result.stdout));
    assert.equal(Array.isArray(payload), true);
    assert.equal(payload.some((entry) => entry.name === '/alpha.json'), true);
    assert.equal(payload.some((entry) => entry.name === '/notes.txt'), true);

    result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/edit?edit=/notes.txt']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.equal(parseDumpedRoute(result.stdout), 'hello native');

    result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/edit?download=/alpha.json']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.equal(parseDumpedRoute(result.stdout), '{"alpha":1}');
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper emits info JSON that the original info dialog can render without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'info']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    const payload = parseDumpedJson(result.stdout);

    assert.equal(typeof payload.name, 'string');
    assert.equal(typeof payload.ver, 'string');
    assert.equal(typeof payload.release, 'string');
    assert.equal(typeof payload.arch, 'string');
    assert.equal(typeof payload.core, 'string');
    assert.equal(typeof payload.mac, 'string');
    assert.equal(typeof payload.uptime, 'number');
    assert.equal(typeof payload.time, 'string');
    assert.equal(typeof payload.freeheap, 'number');
    assert.equal(typeof payload.clock, 'number');
    assert.equal(typeof payload.flash, 'number');

    assert.equal(typeof payload.wifi, 'object');
    assert.equal(typeof payload.wifi.rssi, 'number');
    assert.equal(typeof payload.wifi.signal, 'number');
    assert.equal(typeof payload.wifi.channel, 'number');
    assert.equal(typeof payload.wifi.ap, 'boolean');

    assert.equal(typeof payload.fs, 'object');
    assert.equal(typeof payload.fs.u, 'number');
    assert.equal(typeof payload.fs.t, 'number');
    assert.equal(typeof payload.fs.pmt, 'number');

    assert.equal(typeof payload.leds, 'object');
    assert.equal(typeof payload.leds.count, 'number');
    assert.equal(typeof payload.leds.fps, 'number');
    assert.equal(typeof payload.leds.pwr, 'number');
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper serves settings pages and their shared nested assets without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    let result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/settings/common.js']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    let routeText = parseDumpedRoute(result.stdout);
    assert.match(routeText, /function loadResources\(files, init\)/);
    assert.match(routeText, /function fetchPinInfo\(cb, retries=5\)/);

    result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/settings/style.css']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    routeText = parseDumpedRoute(result.stdout);
    assert.match(routeText, /\.toprow/);
    assert.match(routeText, /#toast/);

    const pageExpectations = [
      ['/settings/wifi', /WiFi & Network Settings/, /loadJS\(getURL\('\/settings\/s\.js\?p=1'\)/],
      ['/settings/leds', /LED setup/, /fetchPinInfo\(pinDropdowns\)/],
      ['/settings/ui', /User Interface/, /loadJS\(getURL\('\/settings\/s\.js\?p=3'\)/],
      ['/settings/sync', /Sync setup/, /fetchPinInfo\(pinDropdowns\)/],
      ['/settings/time', /Time setup/, /loadJS\(getURL\('\/settings\/s\.js\?p=5'\)/],
      ['/settings/sec', /Security & Update Setup/, /loadJS\(getURL\('\/settings\/s\.js\?p=6'\)/],
      ['/settings/um', /Usermod Setup/, /fetchPinInfo\(pinDD\)/],
      ['/settings/dmx', /Imma firin ma lazer/, /loadJS\(getURL\('\/settings\/s\.js\?p=7'\)/],
      ['/settings/2D', /2D setup/, /loadJS\(getURL\('\/settings\/s\.js\?p=10'\)/],
      ['/settings/pins', /Pin Info/, /fetchPinInfo\(\(\)=>\{/]
    ];

    for (const [routePath, headingPattern, scriptPattern] of pageExpectations) {
      result = runNativeCommand(['--config-dir', configDir, '--dump-route', routePath]);
      assert.equal(result.status, 0, result.stderr || result.stdout);
      routeText = parseDumpedRoute(result.stdout);
      assert.match(routeText, headingPattern);
      assert.match(routeText, scriptPattern);
      assert.match(routeText, /common\.js/);
      assert.match(routeText, /style\.css/);
    }

    result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/settings/s.js?p=11']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    routeText = parseDumpedRoute(result.stdout);
    assert.match(routeText, /d\.um_p=\[-1\]/);
    assert.match(routeText, /d\.rsvd=\[\]/);
    assert.match(routeText, /d\.ro_gpio=\[\]/);
    assert.match(routeText, /d\.max_gpio=64/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper treats skin.css as an optional browser asset without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    let result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/skin.css']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.equal(parseDumpedRoute(result.stdout), '');

    fs.writeFileSync(path.join(configDir, 'skin.css'), 'body{outline:1px solid red;}\n', 'utf8');

    result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/skin.css']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.equal(parseDumpedRoute(result.stdout), 'body{outline:1px solid red;}');
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper applies usermod settings without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const body = new URLSearchParams({
      'Autosave:enabled': 'false',
      'Autosave:autoSaveAfterSec': '9',
      'Autosave:autoSavePreset': '222',
      'Autosave:autoSaveApplyOnBoot': 'true',
      'Autosave:autoSaveIgnorePresets': 'false'
    }).toString();

    let result = runNativeCommand(['--config-dir', configDir, '--apply-settings', `/settings/um:${body}`, '--dump-json', 'cfg']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    let payload = parseDumpedJson(result.stdout);
    assert.equal(payload.um.Autosave.enabled, false);
    assert.equal(payload.um.Autosave.autoSaveAfterSec, 9);
    assert.equal(payload.um.Autosave.autoSavePreset, 222);
    assert.equal(payload.um.Autosave.autoSaveApplyOnBoot, true);
    assert.equal(payload.um.Autosave.autoSaveIgnorePresets, false);

    const cfg = readJsonFile(path.join(configDir, 'cfg.json'));
    assert.equal(cfg.um.Autosave.enabled, false);
    assert.equal(cfg.um.Autosave.autoSaveAfterSec, 9);
    assert.equal(cfg.um.Autosave.autoSavePreset, 222);
    assert.equal(cfg.um.Autosave.autoSaveApplyOnBoot, true);
    assert.equal(cfg.um.Autosave.autoSaveIgnorePresets, false);

    result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'state']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    payload = parseDumpedJson(result.stdout);
    assert.equal(payload.Autosave.enabled, false);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper applies DMX settings and renders DMX browser pages without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const body = new URLSearchParams({
      PU: '12',
      CN: '4',
      CS: '20',
      CG: '30',
      SL: '2',
      CH1: '1',
      CH2: '2',
      CH3: '3',
      CH4: '5',
      CH5: '6'
    }).toString();

    let result = runNativeCommand(['--config-dir', configDir, '--apply-settings', `/settings/dmx:${body}`, '--dump-json', 'cfg']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    let payload = parseDumpedJson(result.stdout);
    assert.equal(payload.host.dmx.proxyUniverse, 12);
    assert.equal(payload.host.dmx.channelsPerFixture, 4);
    assert.equal(payload.host.dmx.fixtureStartAddress, 20);
    assert.equal(payload.host.dmx.fixtureSpacing, 30);
    assert.equal(payload.host.dmx.startLed, 2);
    assert.deepEqual(payload.host.dmx.fixtureMap.slice(0, 5), [1, 2, 3, 5, 6]);

    result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/settings/s.js?p=7']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    let routeText = parseDumpedRoute(result.stdout);
    assert.match(routeText, /"PU"\]\.value="12"/);
    assert.match(routeText, /"CN"\]\.value="4"/);
    assert.match(routeText, /"CS"\]\.value="20"/);
    assert.match(routeText, /"CG"\]\.value="30"/);
    assert.match(routeText, /"SL"\]\.value="2"/);
    assert.match(routeText, /"CH1"\]\.value="1"/);
    assert.match(routeText, /"CH4"\]\.value="5"/);
    assert.match(routeText, /"CH5"\]\.value="6"/);

    result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/dmxmap']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    routeText = parseDumpedRoute(result.stdout);
    assert.match(routeText, /var CH=\[1,2,3,5,6,/);
    assert.match(routeText, /CN=4;/);
    assert.match(routeText, /CS=20;/);
    assert.match(routeText, /CG=30;/);
    assert.match(routeText, /LC=30;/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper applies 2D settings and renders the 2D settings page without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const body = new URLSearchParams({
      SOMP: '1',
      MPC: '2',
      P0B: '1',
      P0R: '0',
      P0V: '1',
      P0S: 'on',
      P0W: '8',
      P0H: '16',
      P0X: '0',
      P0Y: '0',
      P1B: '0',
      P1R: '1',
      P1V: '0',
      P1W: '8',
      P1H: '16',
      P1X: '8',
      P1Y: '0'
    }).toString();

    let result = runNativeCommand(['--config-dir', configDir, '--apply-settings', `/settings/2D:${body}`, '--dump-json', 'cfg']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    let payload = parseDumpedJson(result.stdout);
    assert.equal(payload.host.matrix.enabled, true);
    assert.equal(payload.host.matrix.panels.length, 2);
    assert.deepEqual(payload.host.matrix.panels[0], {
      bottomStart: 1,
      rightStart: 0,
      vertical: 1,
      serpentine: true,
      xOffset: 0,
      yOffset: 0,
      width: 8,
      height: 16
    });
    assert.deepEqual(payload.host.matrix.panels[1], {
      bottomStart: 0,
      rightStart: 1,
      vertical: 0,
      serpentine: false,
      xOffset: 8,
      yOffset: 0,
      width: 8,
      height: 16
    });

    result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/settings/s.js?p=10']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    const routeText = parseDumpedRoute(result.stdout);
    assert.match(routeText, /"SOMP"\]\.value="1"/);
    assert.match(routeText, /maxPanels=\d+;resetPanels\(\);/);
    assert.match(routeText, /addPanel\(0\);/);
    assert.match(routeText, /addPanel\(1\);/);
    assert.match(routeText, /"PW"\]\.value="8"/);
    assert.match(routeText, /"PH"\]\.value="16"/);
    assert.match(routeText, /"MPC"\]\.value="2"/);
    assert.match(routeText, /"P0B"\]\.value="1"/);
    assert.match(routeText, /"P0S"\]\.checked=true/);
    assert.match(routeText, /"P1R"\]\.value="1"/);
    assert.match(routeText, /"P1X"\]\.value="8"/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper stages and reverts host update packages without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const packagePath = path.join(configDir, 'WLED_1.2.3_native.pkg');
    fs.writeFileSync(packagePath, 'native update payload');

    let result = runNativeCommand(['--config-dir', configDir, '--stage-update-file', packagePath]);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, /Staged update file:/);

    const stagedPackagePath = path.join(configDir, 'pending-update.bin');
    const stagedMetadataPath = path.join(configDir, 'pending-update.json');
    assert.equal(fs.readFileSync(stagedPackagePath, 'utf8'), 'native update payload');
    const stagedMetadata = readJsonFile(stagedMetadataPath);
    assert.equal(stagedMetadata.name, 'WLED_1.2.3_native.pkg');
    assert.equal(stagedMetadata.size, 'native update payload'.length);

    result = runNativeCommand(['--config-dir', configDir, '--dump-route', '/update?revert']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    const routeText = parseDumpedRoute(result.stdout);
    assert.match(routeText, /reverted/i);
    assert.equal(fs.existsSync(stagedPackagePath), false);
    assert.equal(fs.existsSync(stagedMetadataPath), false);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper dumps host network scan JSON without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const injectedScan = JSON.stringify({
      networks: [
        { ssid: 'Studio', rssi: -40, bssid: 'AA:BB:CC:DD:EE:01' },
        { ssid: 'Backup', rssi: -67, bssid: 'AA:BB:CC:DD:EE:02' }
      ]
    });

    const result = childProcess.spawnSync(nativeRunScript, ['--config-dir', configDir, '--dump-json', 'net'], {
      cwd: repoRoot,
      encoding: 'utf8',
      env: { ...process.env, WLED_NATIVE_WIFI_SCAN_JSON: injectedScan }
    });
    assert.equal(result.status, 0, result.stderr || result.stdout);
    const payload = parseDumpedJson(result.stdout);
    assert.equal(Array.isArray(payload.networks), true);
    assert.equal(payload.networks.length, 2);
    assert.equal(payload.networks[0].ssid, 'Studio');
    assert.equal(payload.networks[0].rssi, -40);
    assert.equal(payload.networks[1].bssid, 'AA:BB:CC:DD:EE:02');
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper dumps host pin info JSON without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const result = runNativeCommand(['--config-dir', configDir, '--dump-json', 'pins']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    const payload = parseDumpedJson(result.stdout);
    assert.equal(Array.isArray(payload.pins), true);
    assert.ok(payload.pins.length >= 8);
    assert.deepEqual(payload.pins[0], { p: 0, c: 0, a: false });
    assert.deepEqual(payload.pins.at(-1), { p: 63, c: 0, a: false });
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper dumps host-discovered nodes without binding a server socket', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));
  const registryPath = path.join(configDir, 'native-nodes.json');

  try {
    fs.writeFileSync(path.join(configDir, 'instance-id'), 'AA:BB:CC:DD:EE:01\n');
    fs.writeFileSync(
      path.join(configDir, 'cfg.json'),
      JSON.stringify({
        id: { name: 'Desk Native' },
        host: { sync: { nodeListEnabled: true } }
      })
    );

    const now = Date.now();
    fs.writeFileSync(
      registryPath,
      JSON.stringify({
        entries: [
          {
            instanceId: 'AA:BB:CC:DD:EE:01',
            name: 'Desk Native',
            ip: '127.0.0.1',
            port: 21324,
            updatedAtMs: now,
            type: 128,
            vid: 1700000
          },
          {
            instanceId: 'AA:BB:CC:DD:EE:02',
            name: 'Kitchen Native',
            ip: '127.0.0.1',
            port: 21325,
            updatedAtMs: now - 4000,
            type: 128,
            vid: 1700001
          },
          {
            instanceId: 'AA:BB:CC:DD:EE:03',
            name: 'Stale Native',
            ip: '127.0.0.1',
            port: 21326,
            updatedAtMs: now - 120000,
            type: 128,
            vid: 1699999
          }
        ]
      })
    );

    let result = childProcess.spawnSync(nativeRunScript, ['--config-dir', configDir, '--dump-json', 'nodes'], {
      cwd: repoRoot,
      encoding: 'utf8',
      env: { ...process.env, WLED_NATIVE_NODE_REGISTRY_PATH: registryPath }
    });
    assert.equal(result.status, 0, result.stderr || result.stdout);
    let payload = parseDumpedJson(result.stdout);
    assert.equal(Array.isArray(payload.nodes), true);
    assert.equal(payload.nodes.length, 1);
    assert.equal(payload.nodes[0].name, 'Kitchen Native');
    assert.equal(payload.nodes[0].ip, '127.0.0.1:21325');
    assert.equal(payload.nodes[0].vid, 1700001);
    assert.equal(payload.nodes[0].type, 128);
    assert.equal(payload.nodes[0].age >= 4, true);

    result = childProcess.spawnSync(nativeRunScript, ['--config-dir', configDir, '--dump-json', 'info'], {
      cwd: repoRoot,
      encoding: 'utf8',
      env: { ...process.env, WLED_NATIVE_NODE_REGISTRY_PATH: registryPath }
    });
    assert.equal(result.status, 0, result.stderr || result.stdout);
    payload = parseDumpedJson(result.stdout);
    assert.equal(payload.ndc, 1);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper applies boot presets on startup and persists boot preset changes', async () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  let server;
  try {
    fs.writeFileSync(
      path.join(configDir, 'cfg.json'),
      JSON.stringify({
        def: { ps: 2 }
      })
    );
    fs.writeFileSync(
      path.join(configDir, 'presets.json'),
      JSON.stringify({
        0: {},
        2: {
          n: 'Booted',
          on: true,
          bri: 33,
          transition: 9,
          seg: [{ fx: 4, pal: 6, sx: 88, ix: 144, bri: 201, col: [[4, 5, 6], [0, 0, 0], [0, 0, 0]] }]
        }
      })
    );

    server = await startNativeServer(configDir);

    let infoResponse = await fetch(`${server.listeningUrl}/json/info`, { signal: AbortSignal.timeout(5000) });
    assert.equal(infoResponse.status, 200);
    let info = await infoResponse.json();
    assert.equal(info.leds.bootps, 2);

    let stateResponse = await fetch(`${server.listeningUrl}/json/state`, { signal: AbortSignal.timeout(5000) });
    assert.equal(stateResponse.status, 200);
    let state = await stateResponse.json();
    assert.equal(state.ps, 2);
    assert.equal(state.bri, 33);
    assert.equal(state.transition, 9);
    assert.equal(state.seg[0].fx, 4);
    assert.equal(state.seg[0].pal, 6);

    const updateResponse = await fetch(`${server.listeningUrl}/json/state`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ bri: 99, seg: { fx: 7, pal: 8 }, psave: 3, bootps: 3, n: 'Restart Me' }),
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(updateResponse.status, 200);

    const cfg = readJsonFile(path.join(configDir, 'cfg.json'));
    assert.equal(cfg.def.ps, 3);
    const presets = readJsonFile(path.join(configDir, 'presets.json'));
    assert.equal(presets['3'].n, 'Restart Me');
    assert.equal(presets['3'].bri, 99);
    assert.equal(presets['3'].seg[0].fx, 7);
    assert.equal(presets['3'].seg[0].pal, 8);

    await server.stop();
    server = await startNativeServer(configDir);

    infoResponse = await fetch(`${server.listeningUrl}/json/info`, { signal: AbortSignal.timeout(5000) });
    info = await infoResponse.json();
    assert.equal(info.leds.bootps, 3);

    stateResponse = await fetch(`${server.listeningUrl}/json/state`, { signal: AbortSignal.timeout(5000) });
    state = await stateResponse.json();
    assert.equal(state.ps, 3);
    assert.equal(state.bri, 99);
    assert.equal(state.seg[0].fx, 7);
    assert.equal(state.seg[0].pal, 8);
  } finally {
    if (server) await server.stop();
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper accepts direct preset payloads and saves playlist presets', async () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  let server;
  try {
    server = await startNativeServer(configDir);

    const directResponse = await fetch(`${server.listeningUrl}/json/state`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        pd: 8,
        on: false,
        bri: 44,
        transition: 15,
        seg: [{ fx: 5, pal: 9, sx: 77, ix: 155, bri: 90, col: [[9, 8, 7], [0, 0, 0], [0, 0, 0]] }]
      }),
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(directResponse.status, 200);
    const directState = await directResponse.json();
    assert.equal(directState.state.ps, 8);
    assert.equal(directState.state.on, false);
    assert.equal(directState.state.bri, 44);
    assert.equal(directState.state.seg[0].fx, 5);
    assert.equal(directState.state.seg[0].pal, 9);

    const playlistSaveResponse = await fetch(`${server.listeningUrl}/json/state`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        psave: 12,
        n: 'Cycle',
        on: true,
        o: true,
        playlist: {
          ps: [21, 22],
          dur: [1, 1],
          transition: [7, 7],
          repeat: 1,
          end: 21,
          r: false
        }
      }),
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(playlistSaveResponse.status, 200);

    const presets = readJsonFile(path.join(configDir, 'presets.json'));
    assert.equal(presets['12'].n, 'Cycle');
    assert.deepEqual(presets['12'].playlist.ps, [21, 22]);
    assert.deepEqual(presets['12'].playlist.transition, [7, 7]);
  } finally {
    if (server) await server.stop();
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper runs playlists in server mode and advances to the next preset', async () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  let server;
  try {
    fs.writeFileSync(
      path.join(configDir, 'presets.json'),
      JSON.stringify({
        0: {},
        21: { n: 'One', on: true, bri: 30, seg: [{ fx: 1, pal: 2 }] },
        22: { n: 'Two', on: true, bri: 90, seg: [{ fx: 3, pal: 4 }] }
      })
    );

    server = await startNativeServer(configDir);

    const playlistResponse = await fetch(`${server.listeningUrl}/json/state`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        on: true,
        playlist: {
          ps: [21, 22],
          dur: [1, 1],
          transition: [7, 7],
          repeat: 1,
          end: 21,
          r: false
        }
      }),
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(playlistResponse.status, 200);
    let playlistState = await playlistResponse.json();
    assert.equal(playlistState.state.ps, 21);
    assert.equal(playlistState.state.seg[0].fx, 1);

    const nextResponse = await fetch(`${server.listeningUrl}/json/state`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ np: true }),
      signal: AbortSignal.timeout(5000)
    });
    assert.equal(nextResponse.status, 200);
    playlistState = await nextResponse.json();
    assert.equal(playlistState.state.ps, 22);
    assert.equal(playlistState.state.seg[0].fx, 3);
    assert.equal(playlistState.state.seg[0].pal, 4);
  } finally {
    if (server) await server.stop();
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper serves real effect, fxdata, and palette catalogs', async () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  let server;
  try {
    server = await startNativeServer(configDir);

    const effectsResponse = await fetch(`${server.listeningUrl}/json/effects`, { signal: AbortSignal.timeout(5000) });
    assert.equal(effectsResponse.status, 200);
    const effects = await effectsResponse.json();
    assert.ok(effects.length > 20);
    assert.equal(effects[0], 'Solid');
    assert.ok(effects.includes('Blink'));

    const fxDataResponse = await fetch(`${server.listeningUrl}/json/fxdata`, { signal: AbortSignal.timeout(5000) });
    assert.equal(fxDataResponse.status, 200);
    const fxData = await fxDataResponse.json();
    assert.equal(fxData.length, effects.length);
    assert.match(fxData[0], /^Solid/);

    const palettesResponse = await fetch(`${server.listeningUrl}/json/palettes`, { signal: AbortSignal.timeout(5000) });
    assert.equal(palettesResponse.status, 200);
    const palettes = await palettesResponse.json();
    assert.ok(palettes.length > 10);
    assert.equal(palettes[0], 'Default');
  } finally {
    if (server) await server.stop();
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper resolves logical paths inside the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const result = runNativeCommand(['--config-dir', configDir, '--resolve-path', '/presets.json']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.equal(extractField(result.stdout, 'Resolved path'), path.join(configDir, 'presets.json'));
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper rejects path traversal outside the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const result = runNativeCommand(['--config-dir', configDir, '--resolve-path', '../escape.json']);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /Path traversal is not allowed|escapes the config root/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper reads an existing logical file inside the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const customPath = path.join(configDir, 'custom.json');
    fs.writeFileSync(customPath, '{"hello":"world"}\n', 'utf8');

    const result = runNativeCommand(['--config-dir', configDir, '--read-file', 'custom.json']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, /Read file: custom\.json/);
    assert.match(result.stdout, /"hello":"world"/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper copies an existing logical file inside the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const sourcePath = path.join(configDir, 'source.json');
    const destinationPath = path.join(configDir, 'backup.json');
    fs.writeFileSync(sourcePath, '{"copy":true}\n', 'utf8');

    const result = runNativeCommand(['--config-dir', configDir, '--copy-file', 'source.json:backup.json']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, /Copied file: source\.json -> backup\.json/);
    assert.equal(fs.readFileSync(destinationPath, 'utf8'), '{"copy":true}\n');
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper renames an existing logical file inside the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const sourcePath = path.join(configDir, 'rename-source.json');
    const destinationPath = path.join(configDir, 'rename-destination.json');
    fs.writeFileSync(sourcePath, '{"rename":true}\n', 'utf8');

    const result = runNativeCommand(['--config-dir', configDir, '--rename-file', 'rename-source.json:rename-destination.json']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, /Renamed file: rename-source\.json -> rename-destination\.json/);
    assert.equal(fs.existsSync(sourcePath), false);
    assert.equal(fs.readFileSync(destinationPath, 'utf8'), '{"rename":true}\n');
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper deletes an existing logical file inside the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const deletePath = path.join(configDir, 'delete-me.json');
    fs.writeFileSync(deletePath, '{"delete":true}\n', 'utf8');

    const result = runNativeCommand(['--config-dir', configDir, '--delete-file', 'delete-me.json']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, /Deleted file: delete-me\.json/);
    assert.equal(fs.existsSync(deletePath), false);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper compares logical files inside the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    fs.writeFileSync(path.join(configDir, 'same-a.json'), '{"same":true}\n', 'utf8');
    fs.writeFileSync(path.join(configDir, 'same-b.json'), '{"same":true}\n', 'utf8');
    fs.writeFileSync(path.join(configDir, 'different.json'), '{"same":false}\n', 'utf8');

    const matchingResult = runNativeCommand(['--config-dir', configDir, '--compare-files', 'same-a.json:same-b.json']);
    assert.equal(matchingResult.status, 0, matchingResult.stderr || matchingResult.stdout);
    assert.match(matchingResult.stdout, /Files match: same-a\.json == same-b\.json/);

    const mismatchResult = runNativeCommand(['--config-dir', configDir, '--compare-files', 'same-a.json:different.json']);
    assert.notEqual(mismatchResult.status, 0);
    assert.match(mismatchResult.stderr, /Files differ: same-a\.json != different\.json/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper validates logical JSON files inside the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    fs.writeFileSync(path.join(configDir, 'valid.json'), '{"valid":true}\n', 'utf8');
    fs.writeFileSync(path.join(configDir, 'invalid.json'), '{"valid": }\n', 'utf8');

    const validResult = runNativeCommand(['--config-dir', configDir, '--validate-json', 'valid.json']);
    assert.equal(validResult.status, 0, validResult.stderr || validResult.stdout);
    assert.match(validResult.stdout, /Valid JSON file: valid\.json/);

    const invalidResult = runNativeCommand(['--config-dir', configDir, '--validate-json', 'invalid.json']);
    assert.notEqual(invalidResult.status, 0);
    assert.match(invalidResult.stderr, /Invalid JSON file: invalid\.json/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper creates and restores logical backups inside the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const cfgPath = path.join(configDir, 'cfg.json');
    const backupPath = path.join(configDir, 'bkp.cfg.json');
    fs.writeFileSync(cfgPath, '{"version":1}\n', 'utf8');

    const backupResult = runNativeCommand(['--config-dir', configDir, '--backup-file', 'cfg.json']);
    assert.equal(backupResult.status, 0, backupResult.stderr || backupResult.stdout);
    assert.match(backupResult.stdout, /Created backup: cfg\.json -> bkp\.cfg\.json/);
    assert.equal(fs.readFileSync(backupPath, 'utf8'), '{"version":1}\n');

    fs.writeFileSync(cfgPath, '{"version":2}\n', 'utf8');

    const restoreResult = runNativeCommand(['--config-dir', configDir, '--restore-file', 'cfg.json']);
    assert.equal(restoreResult.status, 0, restoreResult.stderr || restoreResult.stdout);
    assert.match(restoreResult.stdout, /Restored backup: bkp\.cfg\.json -> cfg\.json/);
    assert.equal(fs.readFileSync(cfgPath, 'utf8'), '{"version":1}\n');
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper reports whether a logical backup exists inside the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const beforeResult = runNativeCommand(['--config-dir', configDir, '--has-backup', 'cfg.json']);
    assert.notEqual(beforeResult.status, 0);
    assert.match(beforeResult.stderr, /Backup missing: bkp\.cfg\.json/);

    fs.writeFileSync(path.join(configDir, 'cfg.json'), '{"version":1}\n', 'utf8');
    const backupResult = runNativeCommand(['--config-dir', configDir, '--backup-file', 'cfg.json']);
    assert.equal(backupResult.status, 0, backupResult.stderr || backupResult.stdout);

    const afterResult = runNativeCommand(['--config-dir', configDir, '--has-backup', 'cfg.json']);
    assert.equal(afterResult.status, 0, afterResult.stderr || afterResult.stdout);
    assert.match(afterResult.stdout, /Backup exists: bkp\.cfg\.json/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper lists logical JSON files in the config root', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    fs.writeFileSync(path.join(configDir, 'alpha.json'), '{"alpha":1}\n', 'utf8');
    fs.writeFileSync(path.join(configDir, 'notes.txt'), 'ignore\n', 'utf8');

    const result = runNativeCommand(['--config-dir', configDir, '--list-files']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, /Config files:/);
    assert.match(result.stdout, /alpha\.json/);
    assert.match(result.stdout, / - cfg\.json/);
    assert.doesNotMatch(result.stdout, / - notes\.txt/);
    assert.doesNotMatch(result.stdout, / - wsec\.json/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper blends colors through WLED color core', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const result = runNativeCommand(['--config-dir', configDir, '--blend-color', 'FF0000:0000FF:128']);
    const expected = formatColor(blendColor(0x00FF0000, 0x000000FF, 128));

    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, new RegExp(`Blended color: ${expected}`));
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper adds colors through WLED color core', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const result = runNativeCommand(['--config-dir', configDir, '--add-color', '80402010:40208004:1']);
    const expected = formatColor(addColor(0x10804020, 0x04402080, true));

    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, new RegExp(`Added color: ${expected}`));
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper fades colors through WLED color core', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const result = runNativeCommand(['--config-dir', configDir, '--fade-color', '112233:128:1']);
    const expected = formatColor(fadeColor(0x00112233, 128, true));

    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, new RegExp(`Faded color: ${expected}`));
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper emits a deterministic WLED PRNG sequence for a seed', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    const result = runNativeCommand(['--config-dir', configDir, '--prng-seq', '4660:4']);
    const expected = prngSequence(4660, 4).join(' ');

    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, new RegExp(`PRNG sequence: ${expected}`));
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper runs original playlist logic from a config-root JSON file', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    fs.writeFileSync(
      path.join(configDir, 'playlist.json'),
      JSON.stringify({
        ps: [11, 22, 33],
        dur: [1, 1, 1],
        transition: [5, 6, 7],
        repeat: 1
      }) + '\n',
      'utf8'
    );

    const result = runNativeCommand(['--config-dir', configDir, '--playlist-run', 'playlist.json:3:150']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, /Playlist sequence: 11 22 33/);
    assert.match(result.stdout, /Playlist transition: 700/);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper uses original preset-file logic for init, name lookup, and delete', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    fs.rmSync(path.join(configDir, 'presets.json'), { force: true });

    const initResult = runNativeCommand(['--config-dir', configDir, '--init-presets']);
    assert.equal(initResult.status, 0, initResult.stderr || initResult.stdout);
    assert.deepEqual(readJsonFile(path.join(configDir, 'presets.json')), { '0': {} });

    fs.writeFileSync(
      path.join(configDir, 'presets.json'),
      JSON.stringify({
        0: {},
        1: { n: 'Sunrise' },
        2: { n: 'Evening' }
      }) + '\n',
      'utf8'
    );

    const nameResult = runNativeCommand(['--config-dir', configDir, '--preset-name', '2']);
    assert.equal(nameResult.status, 0, nameResult.stderr || nameResult.stdout);
    assert.match(nameResult.stdout, /Preset name: Evening/);

    const deleteResult = runNativeCommand(['--config-dir', configDir, '--delete-preset', '2']);
    assert.equal(deleteResult.status, 0, deleteResult.stderr || deleteResult.stdout);
    assert.match(deleteResult.stdout, /Deleted preset: 2/);
    assert.deepEqual(readJsonFile(path.join(configDir, 'presets.json')), {
      0: {},
      1: { n: 'Sunrise' },
      2: {}
    });
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});

test('native wrapper uses original cfg helpers for backup, restore, verify, reset, and secret validation', () => {
  const configDir = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-native-'));

  try {
    fs.writeFileSync(path.join(configDir, 'cfg.json'), '{"version":1}\n', 'utf8');
    fs.writeFileSync(path.join(configDir, 'wsec.json'), '{"token":"secret"}\n', 'utf8');

    const backupResult = runNativeCommand(['--config-dir', configDir, '--backup-config']);
    assert.equal(backupResult.status, 0, backupResult.stderr || backupResult.stdout);
    assert.match(backupResult.stdout, /Backed up config: \/cfg\.json/);

    const hasBackupResult = runNativeCommand(['--config-dir', configDir, '--has-config-backup']);
    assert.equal(hasBackupResult.status, 0, hasBackupResult.stderr || hasBackupResult.stdout);
    assert.match(hasBackupResult.stdout, /Config backup exists: \/bkp\.cfg\.json/);

    const verifyConfigResult = runNativeCommand(['--config-dir', configDir, '--verify-config']);
    assert.equal(verifyConfigResult.status, 0, verifyConfigResult.stderr || verifyConfigResult.stdout);
    assert.match(verifyConfigResult.stdout, /Valid config file: \/cfg\.json/);

    const verifySecretsResult = runNativeCommand(['--config-dir', configDir, '--verify-secrets']);
    assert.equal(verifySecretsResult.status, 0, verifySecretsResult.stderr || verifyConfigResult.stdout);
    assert.match(verifySecretsResult.stdout, /Valid secrets file: \/wsec\.json/);

    fs.writeFileSync(path.join(configDir, 'cfg.json'), '{"version":2}\n', 'utf8');
    const restoreResult = runNativeCommand(['--config-dir', configDir, '--restore-config']);
    assert.equal(restoreResult.status, 0, restoreResult.stderr || restoreResult.stdout);
    assert.match(restoreResult.stdout, /Restored config: \/cfg\.json/);
    assert.equal(fs.readFileSync(path.join(configDir, 'cfg.json'), 'utf8'), '{"version":1}\n');

    const resetResult = runNativeCommand(['--config-dir', configDir, '--reset-config']);
    assert.equal(resetResult.status, 0, resetResult.stderr || resetResult.stdout);
    assert.match(resetResult.stdout, /Reset config file: \/cfg\.json/);
    assert.equal(fs.existsSync(path.join(configDir, 'cfg.json')), false);
    assert.equal(fs.existsSync(path.join(configDir, 'rst.cfg.json')), true);
  } finally {
    fs.rmSync(configDir, { recursive: true, force: true });
  }
});
