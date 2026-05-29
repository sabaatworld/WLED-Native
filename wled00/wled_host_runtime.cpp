#ifndef ARDUINO

#include "wled_host_runtime.h"

#include "wled_host_cli.h"
#include "wled_host_core.h"
#include "wled_host_playlist.h"
#include "wled_host_presets.h"
#include "wled_host_storage.h"
#include "prng.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool splitCopySpec(const std::string& copyPathSpec, std::string& sourcePath, std::string& destinationPath) {
  const size_t separator = copyPathSpec.find(':');
  if (separator == std::string::npos) return false;
  sourcePath = copyPathSpec.substr(0, separator);
  destinationPath = copyPathSpec.substr(separator + 1);
  return !sourcePath.empty() && !destinationPath.empty();
}

bool handleSplitPathSpec(const char* optionName, const std::string& pathSpec, std::string& sourcePath, std::string& destinationPath) {
  if (splitCopySpec(pathSpec, sourcePath, destinationPath)) return true;
  std::cerr << "Invalid value for " << optionName << ": " << pathSpec << '\n';
  return false;
}

bool parseUnsignedValue(const std::string& value, unsigned long& parsedValue) {
  if (value.empty()) return false;
  char* end = nullptr;
  parsedValue = std::strtoul(value.c_str(), &end, 10);
  return end && *end == '\0';
}

bool parseByteValue(const std::string& value, uint8_t& parsedValue) {
  unsigned long rawValue = 0;
  if (!parseUnsignedValue(value, rawValue) || rawValue > 255UL) return false;
  parsedValue = static_cast<uint8_t>(rawValue);
  return true;
}

bool parseUInt16Value(const std::string& value, uint16_t& parsedValue) {
  unsigned long rawValue = 0;
  if (!parseUnsignedValue(value, rawValue) || rawValue > 65535UL) return false;
  parsedValue = static_cast<uint16_t>(rawValue);
  return true;
}

bool parsePresetIdSpec(const char* optionName, const std::string& spec, uint8_t& presetId) {
  if (!parseByteValue(spec, presetId) || presetId == 0) {
    std::cerr << "Invalid value for " << optionName << ": " << spec << '\n';
    return false;
  }
  return true;
}

std::string formatColor(uint32_t color) {
  std::ostringstream output;
  output << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << color;
  return output.str();
}

bool parseHexColor(const std::string& value, uint32_t& color) {
  std::string normalized = value;
  if (!normalized.empty() && normalized.front() == '#') normalized.erase(normalized.begin());

  uint8_t rgbw[4] = {0, 0, 0, 0};
  if (!colorFromHexString(rgbw, normalized.c_str())) return false;
  color = RGBW32(rgbw[0], rgbw[1], rgbw[2], rgbw[3]);
  return true;
}

bool parseBlendColorSpec(const std::string& spec, uint32_t& leftColor, uint32_t& rightColor, uint8_t& blendAmount) {
  std::string leftSpec;
  std::string rightSpec;
  if (!handleSplitPathSpec("--blend-color", spec, leftSpec, rightSpec)) return false;

  const size_t separator = rightSpec.find(':');
  if (separator == std::string::npos) {
    std::cerr << "Invalid value for --blend-color: " << spec << '\n';
    return false;
  }

  const std::string blendSpec = rightSpec.substr(separator + 1);
  rightSpec = rightSpec.substr(0, separator);

  if (!parseHexColor(leftSpec, leftColor) || !parseHexColor(rightSpec, rightColor) || !parseByteValue(blendSpec, blendAmount)) {
    std::cerr << "Invalid value for --blend-color: " << spec << '\n';
    return false;
  }
  return true;
}

bool parseFadeColorSpec(const std::string& spec, uint32_t& inputColor, uint8_t& fadeAmount, bool& videoFade) {
  std::string colorSpec;
  std::string amountSpec;
  if (!handleSplitPathSpec("--fade-color", spec, colorSpec, amountSpec)) return false;

  std::string videoSpec;
  const size_t separator = amountSpec.find(':');
  if (separator != std::string::npos) {
    videoSpec = amountSpec.substr(separator + 1);
    amountSpec = amountSpec.substr(0, separator);
  }

  if (!parseHexColor(colorSpec, inputColor) || !parseByteValue(amountSpec, fadeAmount)) {
    std::cerr << "Invalid value for --fade-color: " << spec << '\n';
    return false;
  }

  videoFade = false;
  if (!videoSpec.empty()) {
    uint8_t videoFlag = 0;
    if (!parseByteValue(videoSpec, videoFlag) || videoFlag > 1U) {
      std::cerr << "Invalid value for --fade-color: " << spec << '\n';
      return false;
    }
    videoFade = videoFlag == 1U;
  }

  return true;
}

bool parsePrngSequenceSpec(const std::string& spec, uint16_t& seed, uint8_t& count) {
  std::string seedSpec;
  std::string countSpec;
  if (!handleSplitPathSpec("--prng-seq", spec, seedSpec, countSpec)) return false;
  if (!parseUInt16Value(seedSpec, seed) || !parseByteValue(countSpec, count) || count == 0) {
    std::cerr << "Invalid value for --prng-seq: " << spec << '\n';
    return false;
  }
  return true;
}

bool parsePlaylistRunSpec(const std::string& spec, std::string& playlistPath, uint8_t& steps, uint32_t& tickMs) {
  const size_t tickSeparator = spec.rfind(':');
  if (tickSeparator == std::string::npos) {
    std::cerr << "Invalid value for --playlist-run: " << spec << '\n';
    return false;
  }

  const size_t stepSeparator = spec.rfind(':', tickSeparator - 1);
  if (stepSeparator == std::string::npos) {
    std::cerr << "Invalid value for --playlist-run: " << spec << '\n';
    return false;
  }

  playlistPath = spec.substr(0, stepSeparator);
  const std::string stepSpec = spec.substr(stepSeparator + 1, tickSeparator - stepSeparator - 1);
  const std::string tickSpec = spec.substr(tickSeparator + 1);

  unsigned long parsedSteps = 0;
  unsigned long parsedTickMs = 0;
  if (playlistPath.empty() || !parseUnsignedValue(stepSpec, parsedSteps) || parsedSteps == 0 || parsedSteps > 255UL ||
      !parseUnsignedValue(tickSpec, parsedTickMs)) {
    std::cerr << "Invalid value for --playlist-run: " << spec << '\n';
    return false;
  }

  steps = static_cast<uint8_t>(parsedSteps);
  tickMs = static_cast<uint32_t>(parsedTickMs);
  return true;
}

struct HostRuntimeState {
  HostStorageLayout storage;
  std::string instanceId;
};

bool initialiseHostRuntime(const HostCliOptions& options, HostRuntimeState& state, std::string& error) {
  state.storage = buildHostStorageLayout(resolveHostConfigDirectory(options.configDir));
  return bootstrapHostStorage(state.storage, state.instanceId, error);
}

void printRuntimeSummary(const HostCliOptions& options, const HostRuntimeState& state) {
  std::cout
    << "WLED host runtime bootstrap\n"
    << "Config root: " << state.storage.configDir.string() << '\n'
    << "Instance ID: " << state.instanceId << '\n'
    << "Identity file: " << state.storage.instanceIdFile.string() << '\n'
    << "Config file: " << state.storage.cfgFile.string() << '\n'
    << "Secrets file: " << state.storage.wsecFile.string() << '\n'
    << "Presets file: " << state.storage.presetsFile.string() << '\n'
    << "Bind host: " << options.host << '\n'
    << "Bind port: " << options.port << '\n'
    << "Log level: " << options.logLevel << '\n'
    << "Core status: color-utils+prng+playlist+presets enabled\n"
    << "Runtime status: bootstrap-only\n";
}

} // namespace

// AI: below section was generated by an AI
int runWledHost(int argc, char** argv) {
  const HostCliParseResult parseResult = parseHostCliArgs(argc, argv);
  if (!parseResult.ok) {
    std::cerr << parseResult.error << '\n' << '\n';
    printHostCliHelp(std::cerr);
    return 1;
  }

  if (parseResult.options.showHelp) {
    printHostCliHelp(std::cout);
    return 0;
  }

  if (parseResult.options.showVersion) {
    printHostCliVersion(std::cout);
    return 0;
  }

  HostRuntimeState runtimeState;
  std::string error;
  if (!initialiseHostRuntime(parseResult.options, runtimeState, error)) {
    std::cerr << error << '\n';
    return 1;
  }
  setActiveHostStorageLayout(&runtimeState.storage);

  printRuntimeSummary(parseResult.options, runtimeState);

  if (!parseResult.options.resolvePath.empty()) {
    std::filesystem::path resolvedPath;
    if (!resolveHostStoragePath(runtimeState.storage, parseResult.options.resolvePath, resolvedPath, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Resolved path: " << resolvedPath.string() << '\n';
  }

  if (!parseResult.options.readPath.empty()) {
    std::string content;
    if (!readHostStorageFile(runtimeState.storage, parseResult.options.readPath, content, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Read file: " << parseResult.options.readPath << '\n';
    std::cout << content;
    if (!content.empty() && content.back() != '\n') std::cout << '\n';
  }

  if (!parseResult.options.copyPathSpec.empty()) {
    std::string sourcePath;
    std::string destinationPath;
    if (!splitCopySpec(parseResult.options.copyPathSpec, sourcePath, destinationPath)) {
      std::cerr << "Invalid value for --copy-file: " << parseResult.options.copyPathSpec << '\n';
      return 1;
    }
    if (!copyHostStorageFile(runtimeState.storage, sourcePath, destinationPath, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Copied file: " << sourcePath << " -> " << destinationPath << '\n';
  }

  if (!parseResult.options.renamePathSpec.empty()) {
    std::string sourcePath;
    std::string destinationPath;
    if (!splitCopySpec(parseResult.options.renamePathSpec, sourcePath, destinationPath)) {
      std::cerr << "Invalid value for --rename-file: " << parseResult.options.renamePathSpec << '\n';
      return 1;
    }
    if (!renameHostStorageFile(runtimeState.storage, sourcePath, destinationPath, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Renamed file: " << sourcePath << " -> " << destinationPath << '\n';
  }

  if (!parseResult.options.deletePath.empty()) {
    if (!deleteHostStorageFile(runtimeState.storage, parseResult.options.deletePath, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Deleted file: " << parseResult.options.deletePath << '\n';
  }

  if (!parseResult.options.comparePathSpec.empty()) {
    std::string firstPath;
    std::string secondPath;
    if (!handleSplitPathSpec("--compare-files", parseResult.options.comparePathSpec, firstPath, secondPath)) return 1;

    bool identical = false;
    if (!compareHostStorageFiles(runtimeState.storage, firstPath, secondPath, identical, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    if (!identical) {
      std::cerr << "Files differ: " << firstPath << " != " << secondPath << '\n';
      return 1;
    }
    std::cout << "Files match: " << firstPath << " == " << secondPath << '\n';
  }

  if (!parseResult.options.validatePath.empty()) {
    if (!validateHostStorageJsonFile(runtimeState.storage, parseResult.options.validatePath, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Valid JSON file: " << parseResult.options.validatePath << '\n';
  }

  if (!parseResult.options.backupPath.empty()) {
    std::string backupPath;
    if (!createHostStorageBackup(runtimeState.storage, parseResult.options.backupPath, backupPath, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Created backup: " << parseResult.options.backupPath << " -> " << backupPath << '\n';
  }

  if (!parseResult.options.restorePath.empty()) {
    std::string backupPath;
    if (!restoreHostStorageBackup(runtimeState.storage, parseResult.options.restorePath, backupPath, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Restored backup: " << backupPath << " -> " << parseResult.options.restorePath << '\n';
  }

  if (!parseResult.options.hasBackupPath.empty()) {
    std::string backupPath;
    if (!hostStorageBackupExists(runtimeState.storage, parseResult.options.hasBackupPath, backupPath, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Backup exists: " << backupPath << '\n';
  }

  if (parseResult.options.listFiles) {
    std::vector<std::string> fileNames;
    if (!listHostStorageFiles(runtimeState.storage, fileNames, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Config files:\n";
    for (const std::string& fileName : fileNames) {
      std::cout << " - " << fileName << '\n';
    }
  }

  if (!parseResult.options.blendColorSpec.empty()) {
    uint32_t leftColor = 0;
    uint32_t rightColor = 0;
    uint8_t blendAmount = 0;
    if (!parseBlendColorSpec(parseResult.options.blendColorSpec, leftColor, rightColor, blendAmount)) return 1;
    const uint32_t blendedColor = color_blend(leftColor, rightColor, blendAmount);
    std::cout << "Blended color: " << formatColor(blendedColor) << '\n';
  }

  if (!parseResult.options.fadeColorSpec.empty()) {
    uint32_t inputColor = 0;
    uint8_t fadeAmount = 0;
    bool videoFade = false;
    if (!parseFadeColorSpec(parseResult.options.fadeColorSpec, inputColor, fadeAmount, videoFade)) return 1;
    const uint32_t fadedColor = color_fade(inputColor, fadeAmount, videoFade);
    std::cout << "Faded color: " << formatColor(fadedColor) << '\n';
  }

  if (!parseResult.options.prngSequenceSpec.empty()) {
    uint16_t seed = 0;
    uint8_t count = 0;
    if (!parsePrngSequenceSpec(parseResult.options.prngSequenceSpec, seed, count)) return 1;

    PRNG prng(seed);
    std::cout << "PRNG sequence:";
    for (uint8_t i = 0; i < count; ++i) {
      std::cout << ' ' << prng.random16();
    }
    std::cout << '\n';
  }

  if (!parseResult.options.playlistRunSpec.empty()) {
    std::string playlistPath;
    uint8_t steps = 0;
    uint32_t tickMs = 0;
    if (!parsePlaylistRunSpec(parseResult.options.playlistRunSpec, playlistPath, steps, tickMs)) return 1;

    std::string playlistJson;
    if (!readHostStorageFile(runtimeState.storage, playlistPath, playlistJson, error)) {
      std::cerr << error << '\n';
      return 1;
    }

    resetHostPlaylistState();
    DynamicJsonDocument playlistDoc(2048);
    const DeserializationError deserializeError = deserializeJson(playlistDoc, playlistJson);
    if (deserializeError) {
      std::cerr << "Invalid playlist JSON: " << deserializeError.c_str() << '\n';
      return 1;
    }

    JsonObject playlistObject = playlistDoc.as<JsonObject>();
    if (playlistObject.isNull() || loadPlaylist(playlistObject, 1) < 0) {
      std::cerr << "Unable to load playlist from: " << playlistPath << '\n';
      return 1;
    }

    for (uint8_t step = 0; step < steps; ++step) {
      advanceHostMillis(tickMs);
      handlePlaylist();
    }

    std::cout << "Playlist sequence:";
    for (byte preset : getAppliedPlaylistPresets()) {
      std::cout << ' ' << static_cast<unsigned>(preset);
    }
    std::cout << '\n';
    std::cout << "Playlist transition: " << strip.lastTransitionMs << '\n';

    unloadPlaylist();
  }

  if (parseResult.options.initPresets) {
    initPresetsFile();
    std::cout << "Initialized presets file: " << getPresetsFileName() << '\n';
  }

  if (!parseResult.options.presetNameSpec.empty()) {
    uint8_t presetId = 0;
    if (!parsePresetIdSpec("--preset-name", parseResult.options.presetNameSpec, presetId)) return 1;

    String presetName;
    if (!getPresetName(presetId, presetName)) {
      std::cerr << "Preset name not found: " << static_cast<unsigned>(presetId) << '\n';
      return 1;
    }
    std::cout << "Preset name: " << presetName << '\n';
  }

  if (!parseResult.options.deletePresetSpec.empty()) {
    uint8_t presetId = 0;
    if (!parsePresetIdSpec("--delete-preset", parseResult.options.deletePresetSpec, presetId)) return 1;
    deletePreset(presetId);
    std::cout << "Deleted preset: " << static_cast<unsigned>(presetId) << '\n';
  }

  return 0;
}
// AI: end

#endif // !ARDUINO
