#ifndef WLED_HOST_PLAYLIST_H
#define WLED_HOST_PLAYLIST_H

#ifndef ARDUINO

#include <algorithm>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "wled_host_core.h"
#include "src/dependencies/json/ArduinoJson-v6.h"

#define F(x) x
#define DEBUG_PRINTLN(...)
#define random hw_random

template <typename T, typename U, typename V>
constexpr auto constrain(T value, U lower, V upper) -> typename std::common_type<T, U, V>::type {
  using Common = typename std::common_type<T, U, V>::type;
  return std::clamp(static_cast<Common>(value), static_cast<Common>(lower), static_cast<Common>(upper));
}

#define PL_OPTION_SHUFFLE 0x01
#define PL_OPTION_RESTORE 0x02

struct HostPlaylistStrip {
  void setTransition(uint16_t ms) { lastTransitionMs = ms; }
  uint16_t lastTransitionMs = 0;
};

extern HostPlaylistStrip strip;
extern uint16_t transitionDelay;
extern bool jsonTransitionOnce;
extern bool nightlightActive;
extern byte bri;
extern bool doAdvancePlaylist;
extern int16_t currentPlaylist;
extern byte currentPreset;

uint32_t millis();
void setHostMillis(uint32_t value);
void advanceHostMillis(uint32_t deltaMs);

bool applyPresetFromPlaylist(byte index);
void resetHostPlaylistState();
const std::vector<byte>& getAppliedPlaylistPresets();

int16_t loadPlaylist(JsonObject playlistObject, byte presetId = 0);
void handlePlaylist();
void unloadPlaylist();

#endif

#endif // WLED_HOST_PLAYLIST_H
