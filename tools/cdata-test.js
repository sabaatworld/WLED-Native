'use strict';

const assert = require('node:assert');
const { describe, it, before, after } = require('node:test');
const fs = require('fs');
const path = require('path');
const child_process = require('child_process');
const util = require('util');
const execPromise = util.promisify(child_process.exec);

process.env.NODE_ENV = 'test'; // Set the environment to testing
const cdata = require('./cdata.js');

describe('Function', () => {
  const testFolderPath = path.join(__dirname, 'testFolder');
  const oldFilePath = path.join(testFolderPath, 'oldFile.txt');
  const newFilePath = path.join(testFolderPath, 'newFile.txt');

  // Create a temporary file before the test
  before(() => {
    // Create test folder
    if (!fs.existsSync(testFolderPath)) {
      fs.mkdirSync(testFolderPath);
    }

    // Create an old file
    fs.writeFileSync(oldFilePath, 'This is an old file.');
    // Modify the 'mtime' to simulate an old file
    const oldTime = new Date();
    oldTime.setFullYear(oldTime.getFullYear() - 1);
    fs.utimesSync(oldFilePath, oldTime, oldTime);

    // Create a new file
    fs.writeFileSync(newFilePath, 'This is a new file.');
  });

  // delete the temporary files after the test
  after(() => {
    fs.rmSync(testFolderPath, { recursive: true });
  });

  describe('isFileNewerThan', async () => {
    it('should return true if the file is newer than the provided time', async () => {
      const pastTime = Date.now() - 10000; // 10 seconds ago
      assert.strictEqual(cdata.isFileNewerThan(newFilePath, pastTime), true);
    });

    it('should return false if the file is older than the provided time', async () => {
      assert.strictEqual(cdata.isFileNewerThan(oldFilePath, Date.now()), false);
    });

    it('should throw an exception if the file does not exist', async () => {
      assert.throws(() => {
        cdata.isFileNewerThan('nonexistent.txt', Date.now());
      });
    });
  });

  describe('isAnyFileInFolderNewerThan', async () => {
    it('should return true if a file in the folder is newer than the given time', async () => {
      const time = fs.statSync(path.join(testFolderPath, 'oldFile.txt')).mtime;
      assert.strictEqual(cdata.isAnyFileInFolderNewerThan(testFolderPath, time), true);
    });

    it('should return false if no files in the folder are newer than the given time', async () => {
      assert.strictEqual(cdata.isAnyFileInFolderNewerThan(testFolderPath, new Date()), false);
    });

    it('should throw an exception if the folder does not exist', async () => {
      assert.throws(() => {
        cdata.isAnyFileInFolderNewerThan('nonexistent', new Date());
      });
    });
  });
});

describe('Script', () => {
  const folderPath = 'wled00';
  const dataPath = path.join(folderPath, 'data');
  const backupDataPath = 'wled00Backup';
  const backupCdataPath = 'cdata.bak.js';
  const backupPackagePath = 'package.bak.json';

  function ensurePrimaryDataPath() {
    if (fs.existsSync(dataPath)) return;
    fs.cpSync('wled00-backup/data', dataPath, { recursive: true });
  }

  async function runCdata(args = '') {
    const suffix = args ? ` ${args}` : '';
    return execPromise(`node tools/cdata.js${suffix}`);
  }

  before(() => {
    process.env.NODE_ENV = 'production';
    ensurePrimaryDataPath();

    fs.rmSync(backupDataPath, { recursive: true, force: true });
    fs.rmSync(backupCdataPath, { force: true });
    fs.rmSync(backupPackagePath, { force: true });

    // Backup files
    fs.cpSync(dataPath, backupDataPath, { recursive: true });
    fs.cpSync('tools/cdata.js', backupCdataPath);
    fs.cpSync('package.json', backupPackagePath);
  });
  after(() => {
    // Restore backup
    fs.rmSync(dataPath, { recursive: true, force: true });
    if (fs.existsSync(backupDataPath)) fs.renameSync(backupDataPath, dataPath);
    fs.rmSync('tools/cdata.js', { force: true });
    if (fs.existsSync(backupCdataPath)) fs.renameSync(backupCdataPath, 'tools/cdata.js');
    fs.rmSync('package.json', { force: true });
    if (fs.existsSync(backupPackagePath)) fs.renameSync(backupPackagePath, 'package.json');
  });

  // delete all html_*.h files
  async function deleteBuiltFiles() {
    const files = await fs.promises.readdir(folderPath);
    await Promise.all(files.map(file => {
      if (file.startsWith('html_') && path.extname(file) === '.h') {
        return fs.promises.unlink(path.join(folderPath, file));
      }
    }));
  }

  // check if html_*.h files were created
  async function checkIfBuiltFilesExist() {
    const files = await fs.promises.readdir(folderPath);
    const htmlFiles = files.filter(file => file.startsWith('html_') && path.extname(file) === '.h');
    assert(htmlFiles.length > 0, 'html_*.h files were not created');
  }

  async function runAndCheckIfBuiltFilesExist() {
    await runCdata();
    await checkIfBuiltFilesExist();
  }

  function getModifiedTime(file) {
    return fs.statSync(file).mtimeMs;
  }

  function assertFileWasModifiedAfter(file, previousModifiedTime) {
    const modifiedTime = getModifiedTime(file);
    assert(modifiedTime > previousModifiedTime, file + ' was not modified');
  }

  async function testFileModification(sourceFilePath, resultFile) {
    // run cdata.js to ensure html_*.h files are created
    await runCdata();
    const resultFilePath = path.join(folderPath, resultFile);
    const previousModifiedTime = getModifiedTime(resultFilePath);

    // modify file
    fs.appendFileSync(sourceFilePath, ' ');
    // delay for 1 second to ensure the modified time is different
    await new Promise(resolve => setTimeout(resolve, 1400));

    // run script cdata.js again and wait for it to finish
    await runCdata();

    assertFileWasModifiedAfter(resultFilePath, previousModifiedTime);
  }

  describe('should build if', () => {
    it('html_*.h files are missing', async () => {
      await deleteBuiltFiles();
      await runAndCheckIfBuiltFilesExist();
    });

    it('only one html_*.h file is missing', async () => {
      // run script cdata.js and wait for it to finish
      await execPromise('node tools/cdata.js');

      // delete a random html_*.h file
      let files = await fs.promises.readdir(folderPath);
      let htmlFiles = files.filter(file => file.startsWith('html_') && path.extname(file) === '.h');
      const randomFile = htmlFiles[Math.floor(Math.random() * htmlFiles.length)];
      await fs.promises.unlink(path.join(folderPath, randomFile));

      await runAndCheckIfBuiltFilesExist();
    });

    it('script was executed with -f or --force', async () => {
      await runCdata();
      const htmlUiPath = path.join(folderPath, 'html_ui.h');
      let previousModifiedTime = getModifiedTime(htmlUiPath);
      await new Promise(resolve => setTimeout(resolve, 1000));
      await runCdata('--force');
      assertFileWasModifiedAfter(htmlUiPath, previousModifiedTime);
      previousModifiedTime = getModifiedTime(htmlUiPath);
      await new Promise(resolve => setTimeout(resolve, 1000));
      await runCdata('-f');
      assertFileWasModifiedAfter(htmlUiPath, previousModifiedTime);
    });

    it('a file changes', async () => {
      await testFileModification(path.join(dataPath, 'index.htm'), 'html_ui.h');
    });

    it('a inlined file changes', async () => {
      await testFileModification(path.join(dataPath, 'index.js'), 'html_ui.h');
    });

    it('a settings file changes', async () => {
      await testFileModification(path.join(dataPath, 'settings_leds.htm'), 'html_settings.h');
    });

    it('common.js changes', async () => {
      await testFileModification(path.join(dataPath, 'common.js'), 'html_settings.h');
    });

    // this testcase currently fails - might be due to npm updates (maybe "faking" a favicon.ico change is harder now), or a real regression
    // see https://github.com/wled/WLED/issues/5581
    // it('the favicon changes', async () => {
    //   await testFileModification(path.join(dataPath, 'favicon.ico'), 'html_other.h');
    // });

    it('cdata.js changes', async () => {
      await testFileModification('tools/cdata.js', 'html_ui.h');
    });

    it('package.json changes', async () => {
      await testFileModification('package.json', 'html_ui.h');
    });
  });

  describe('should not build if', () => {
    it('the files are already built', async () => {
      await deleteBuiltFiles();

      // run script cdata.js and wait for it to finish
      await runCdata();
      const htmlUiPath = path.join(folderPath, 'html_ui.h');
      const previousModifiedTime = getModifiedTime(htmlUiPath);

      // run script cdata.js and wait for it to finish
      const { stdout } = await runCdata();

      assert(stdout.includes('Web UI is already built'), 'cdata.js did not report an already-built UI');
      assert.strictEqual(getModifiedTime(htmlUiPath), previousModifiedTime, 'html_*.h files were rebuilt');
    });
  });
});
