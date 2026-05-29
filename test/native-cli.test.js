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
    assert.equal(combined.state.on, true);
    assert.equal(combined.state.seg[0].fx, 0);
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
  } finally {
    if (socket && socket.readyState < WebSocket.CLOSING) socket.close();
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
