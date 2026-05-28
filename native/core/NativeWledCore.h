#pragma once

#include "../output/RenderBuffer.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct NativeSegmentState {
  uint16_t id = 0;
  uint16_t start = 0;
  uint16_t stop = 30;
  bool on = true;
  uint8_t bri = 255;
  uint8_t fx = 0;
  uint8_t sx = 128;
  uint8_t ix = 128;
  uint8_t pal = 0;
  NativeColor colors[3] = {
    NativeColor{255, 160, 0, 0, 0},
    NativeColor{0, 0, 0, 0, 0},
    NativeColor{0, 0, 0, 0, 0}
  };
};

struct NativeNightlightState {
  bool active = false;
  uint16_t durationMinutes = 60;
  uint8_t mode = 1;
  uint8_t targetBri = 0;
  uint64_t startedMs = 0;
};

struct NativeCoreState {
  bool on = true;
  uint8_t bri = 128;
  uint16_t transitionMs = 700;
  uint8_t currentPreset = 0;
  int16_t currentPlaylist = -1;
  NativeNightlightState nightlight;
  std::vector<NativeSegmentState> segments;
};

struct NativeCoreSnapshot {
  NativeCoreState state;
  std::vector<std::string> effects;
  std::vector<std::string> effectData;
  std::vector<std::string> palettes;
  uint64_t uptimeSeconds = 0;
};

class NativeWledCore {
public:
  explicit NativeWledCore(std::filesystem::path configRoot = {});

  void begin(size_t ledCount, uint64_t nowMs);
  void tick(uint64_t nowMs);
  void render(NativeRenderBuffer &buffer, uint64_t nowMs);

  void applyJsonState(const std::string &json, uint64_t nowMs);
  bool applyPreset(uint8_t presetId, uint64_t nowMs);
  bool savePreset(uint8_t presetId);
  void advancePlaylist(uint64_t nowMs);

  std::string jsonState(uint64_t nowMs) const;
  std::string jsonInfo(const std::string &version, const std::string &nativeMac, const NativeRenderBuffer &buffer, uint64_t nowMs) const;
  std::string jsonStateInfo(const std::string &version, const std::string &nativeMac, const NativeRenderBuffer &buffer, uint64_t nowMs) const;
  std::string jsonEffects() const;
  std::string jsonFxData() const;
  std::string jsonPalettes() const;
  std::string jsonPaletteData() const;
  std::string presetsJson() const;

  NativeCoreSnapshot snapshot(uint64_t nowMs) const;
  const std::filesystem::path &configRoot() const;

private:
  struct PlaylistEntry {
    uint8_t preset = 0;
    uint32_t durationMs = 10000;
    uint16_t transitionMs = 700;
  };

  struct PlaylistState {
    bool active = false;
    uint8_t presetId = 0;
    size_t index = 0;
    uint16_t repeatRemaining = 1;
    uint8_t endPreset = 0;
    bool shuffle = false;
    uint64_t entryStartedMs = 0;
    std::vector<PlaylistEntry> entries;
  };

  std::filesystem::path _configRoot;
  mutable std::mutex _mutex;
  NativeCoreState _state;
  PlaylistState _playlist;
  uint64_t _startedMs = 0;

  void ensureDefaults(size_t ledCount);
  void ensurePresetsFile() const;
  std::string presetObjectJson(uint8_t presetId) const;
  bool applyPresetJson(uint8_t presetId, const std::string &json, uint64_t nowMs);
  void applyJsonStateLocked(const std::string &json, uint64_t nowMs, bool fromPreset);
  bool loadPlaylistFromJsonLocked(uint8_t presetId, const std::string &json, uint64_t nowMs);
  void applyPlaylistEntryLocked(uint64_t nowMs);
  void advancePlaylistLocked(uint64_t nowMs);
  uint8_t effectiveBrightness(uint64_t nowMs) const;
};
