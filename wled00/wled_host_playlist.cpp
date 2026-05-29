#ifndef ARDUINO

#include "wled_host_playlist.h"

namespace {

uint32_t hostMillis = 0;
std::vector<byte> appliedPlaylistPresets;
std::function<bool(byte)> playlistApplyCallback;

} // namespace

HostPlaylistStrip strip;
uint16_t transitionDelay = 750;
bool jsonTransitionOnce = false;
bool nightlightActive = false;
byte bri = 128;
bool doAdvancePlaylist = false;
int16_t currentPlaylist = -1;
byte currentPreset = 0;

uint32_t millis() {
  return hostMillis;
}

void setHostMillis(uint32_t value) {
  hostMillis = value;
}

void advanceHostMillis(uint32_t deltaMs) {
  hostMillis += deltaMs;
}

bool applyPresetFromPlaylist(byte index) {
  appliedPlaylistPresets.push_back(index);
  currentPreset = index;
  if (playlistApplyCallback) return playlistApplyCallback(index);
  return true;
}

void resetHostPlaylistState() {
  hostMillis = 0;
  appliedPlaylistPresets.clear();
  strip.lastTransitionMs = 0;
  transitionDelay = 750;
  jsonTransitionOnce = false;
  nightlightActive = false;
  bri = 128;
  doAdvancePlaylist = false;
  currentPlaylist = -1;
  currentPreset = 0;
}

const std::vector<byte>& getAppliedPlaylistPresets() {
  return appliedPlaylistPresets;
}

void setHostPlaylistApplyCallback(std::function<bool(byte)> callback) {
  playlistApplyCallback = std::move(callback);
}

#endif // !ARDUINO
