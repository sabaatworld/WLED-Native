'use strict';

const assert = require('node:assert/strict');
const { test } = require('node:test');
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

test('native wrapper prints WLED help text', () => {
  const result = runNativeCommand(['--help']);
  assert.equal(result.status, 0, result.stderr || result.stdout);
  assert.match(result.stdout, /Usage: .*WLED/i);
  assert.match(result.stdout, /--config-dir/);
  assert.match(result.stdout, /--host/);
  assert.match(result.stdout, /--port/);
  assert.match(result.stdout, /--log-level/);
  assert.match(result.stdout, /--version/);
});

test('native wrapper prints WLED version', () => {
  const result = runNativeCommand(['--version']);
  assert.equal(result.status, 0, result.stderr || result.stdout);
  assert.match(result.stdout, /^WLED 17\.0\.0-dev/m);
});
