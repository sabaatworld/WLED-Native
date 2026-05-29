'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { test } = require('node:test');

const repoRoot = path.resolve(__dirname, '..');

const removedPaths = [
  'platformio.ini',
  'platformio_override.sample.ini',
  'requirements.in',
  'requirements.txt',
  'pio-scripts',
  'boards',
  '.github/platformio_release.ini.template',
  '.github/workflows/build.yml',
  '.github/workflows/nightly.yml',
  '.github/workflows/release.yml',
  '.github/workflows/usermods.yml',
  'usermods/platformio_override.usermods.ini'
];

const nativeWorkflowFiles = [
  'AGENTS.md',
  '.github/agent-build.instructions.md',
  '.github/copilot-instructions.md',
  'CONTRIBUTING.md',
  'readme.md',
  '.github/workflows/wled-ci.yml'
];

test('PlatformIO repository artifacts have been removed', () => {
  for (const relativePath of removedPaths) {
    const absolutePath = path.join(repoRoot, relativePath);
    assert.equal(fs.existsSync(absolutePath), false, `${relativePath} should be removed`);
  }
});

test('core native workflow docs no longer prescribe PlatformIO', () => {
  const forbiddenPatterns = [
    /\bPlatformIO\b/i,
    /\bpio run\b/i,
    /\bplatformio\.ini\b/i,
    /\bplatformio_override\b/i,
    /\brequirements\.txt\b/i
  ];

  for (const relativePath of nativeWorkflowFiles) {
    const absolutePath = path.join(repoRoot, relativePath);
    const content = fs.readFileSync(absolutePath, 'utf8');
    for (const pattern of forbiddenPatterns) {
      assert.doesNotMatch(content, pattern, `${relativePath} still contains ${pattern}`);
    }
  }
});
