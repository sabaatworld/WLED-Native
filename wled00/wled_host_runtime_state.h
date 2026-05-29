#ifndef WLED_HOST_RUNTIME_STATE_H
#define WLED_HOST_RUNTIME_STATE_H

#ifndef ARDUINO

#include <array>
#include <cstdint>

struct HostRuntimeState {
  bool on = true;
  uint8_t bri = 128;
  uint16_t transition = 7;
  uint8_t preset = 0;
  int16_t playlist = -1;
  bool playlistActive = false;
  uint8_t segmentBri = 255;
  uint8_t effect = 0;
  uint8_t speed = 128;
  uint8_t intensity = 128;
  uint8_t palette = 0;
  uint16_t ledCount = 30;
  std::array<std::array<uint8_t, 4>, 3> colors = {{
    {{255, 160, 0, 0}},
    {{0, 0, 0, 0}},
    {{0, 0, 0, 0}}
  }};
};

#endif

#endif // WLED_HOST_RUNTIME_STATE_H
