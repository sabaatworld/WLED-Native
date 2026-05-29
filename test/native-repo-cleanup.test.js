'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { test } = require('node:test');

const repoRoot = path.resolve(__dirname, '..');
const joinParts = (...parts) => parts.join('');

const oldBuildToolName = joinParts('plat', 'form', 'io');
const legacyConfigName = joinParts(oldBuildToolName, '.ini');
const legacyOverrideSampleName = joinParts(oldBuildToolName, '_override', '.sample', '.ini');
const legacyRequirementsName = joinParts('requirements', '.txt');
const legacyRequirementsInName = joinParts('requirements', '.in');
const legacyHelperDir = joinParts('pi', 'o-scripts');
const legacyReleaseTemplate = `.github/${joinParts(oldBuildToolName, '_release', '.ini', '.template')}`;
const legacyUsermodsOverride = `usermods/${joinParts(oldBuildToolName, '_override', '.usermods', '.ini')}`;

const removedPaths = [
  legacyConfigName,
  legacyOverrideSampleName,
  legacyRequirementsInName,
  legacyRequirementsName,
  legacyHelperDir,
  'boards',
  'docs/esp-idf.instructions.md',
  legacyReleaseTemplate,
  '.github/workflows/build.yml',
  '.github/workflows/nightly.yml',
  '.github/workflows/release.yml',
  '.github/workflows/usermods.yml',
  legacyUsermodsOverride,
  'tools/WLED_ESP32-wrover_4MB.csv',
  'tools/WLED_ESP32_16MB.csv',
  'tools/WLED_ESP32_16MB_9MB_FS.csv',
  'tools/WLED_ESP32_2MB_noOTA.csv',
  'tools/WLED_ESP32_32MB.csv',
  'tools/WLED_ESP32_4MB_1MB_FS.csv',
  'tools/WLED_ESP32_4MB_256KB_FS.csv',
  'tools/WLED_ESP32_4MB_512KB_FS.csv',
  'tools/WLED_ESP32_4MB_700k_FS.csv',
  'tools/WLED_ESP32_8MB.csv',
  'tools/partitions-16MB_spiffs-tinyuf2.csv',
  'tools/partitions-4MB_spiffs-tinyuf2.csv',
  'tools/partitions-8MB_spiffs-tinyuf2.csv',
  'usermods/pixels_dice_tray/WLED_ESP32_4MB_64KB_FS.csv'
];

const removedUsermodDirs = [
  'usermods/ADS1115_v2',
  'usermods/AHT10_v2',
  'usermods/Animated_Staircase',
  'usermods/Battery',
  'usermods/BH1750_v2',
  'usermods/BME280_v2',
  'usermods/BME68X_v2',
  'usermods/DHT',
  'usermods/EleksTube_IPS',
  'usermods/Enclosure_with_OLED_temp_ESP07',
  'usermods/Fix_unreachable_netservices_v2',
  'usermods/INA226_v2',
  'usermods/Internal_Temperature_v2',
  'usermods/JSON_IR_remote',
  'usermods/LD2410_v2',
  'usermods/LDR_Dusk_Dawn_v2',
  'usermods/MAX17048_v2',
  'usermods/MY9291',
  'usermods/PIR_sensor_switch',
  'usermods/PWM_fan',
  'usermods/RTC',
  'usermods/SN_Photoresistor',
  'usermods/ST7789_display',
  'usermods/Si7021_MQTT_HA',
  'usermods/TTGO-T-Display',
  'usermods/Temperature',
  'usermods/VL53L0X_gestures',
  'usermods/Wemos_D1_mini+Wemos32_mini_shield',
  'usermods/battery_keypad_controller',
  'usermods/buzzer',
  'usermods/deep_sleep',
  'usermods/mpu6050_imu',
  'usermods/multi_relay',
  'usermods/photoresistor_sensor_mqtt_v1',
  'usermods/pixels_dice_tray',
  'usermods/pwm_outputs',
  'usermods/quinled-an-penta',
  'usermods/RelayBlinds',
  'usermods/rgb-rotary-encoder',
  'usermods/rotary_encoder_change_effect',
  'usermods/sd_card',
  'usermods/sensors_to_mqtt',
  'usermods/sht',
  'usermods/usermod_rotary_brightness_color',
  'usermods/usermod_v2_four_line_display_ALT',
  'usermods/usermod_v2_RF433',
  'usermods/usermod_v2_rotary_encoder_ui_ALT',
  'usermods/wireguard'
];

const nativeWorkflowFiles = [
  'AGENTS.md',
  '.github/agent-build.instructions.md',
  '.github/copilot-instructions.md',
  'CONTRIBUTING.md',
  'readme.md',
  '.github/workflows/wled-ci.yml'
];

test('legacy firmware repository artifacts have been removed', () => {
  for (const relativePath of removedPaths) {
    const absolutePath = path.join(repoRoot, relativePath);
    assert.equal(fs.existsSync(absolutePath), false, `${relativePath} should be removed`);
  }
});

test('ESP-only bundled usermods have been removed from the native product path', () => {
  for (const relativePath of removedUsermodDirs) {
    const absolutePath = path.join(repoRoot, relativePath);
    assert.equal(fs.existsSync(absolutePath), false, `${relativePath} should be removed`);
  }
});

test('core native workflow docs no longer prescribe the old firmware toolchain', () => {
  const forbiddenPatterns = [
    new RegExp(`\\b${joinParts('Platform', 'IO')}\\b`, 'i'),
    new RegExp(`\\b${joinParts('pi', 'o run')}\\b`, 'i'),
    new RegExp(`\\b${joinParts(oldBuildToolName, '\\.ini')}\\b`, 'i'),
    new RegExp(`\\b${joinParts(oldBuildToolName, '_override')}\\b`, 'i'),
    new RegExp(`\\b${joinParts('requirements', '\\.txt')}\\b`, 'i')
  ];

  for (const relativePath of nativeWorkflowFiles) {
    const absolutePath = path.join(repoRoot, relativePath);
    const content = fs.readFileSync(absolutePath, 'utf8');
    for (const pattern of forbiddenPatterns) {
      assert.doesNotMatch(content, pattern, `${relativePath} still contains ${pattern}`);
    }
  }
});
