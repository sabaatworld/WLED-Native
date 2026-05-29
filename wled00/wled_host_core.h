#ifndef WLED_HOST_CORE_H
#define WLED_HOST_CORE_H

#ifndef ARDUINO

#include <cstdint>
#include <random>

using byte = uint8_t;

#ifndef BLACK
  #define BLACK (uint32_t)0x000000
#endif

#define RGBW32(r,g,b,w) (uint32_t((byte(w) << 24) | (byte(r) << 16) | (byte(g) << 8) | (byte(b))))

uint32_t hw_random();
uint32_t hw_random(uint32_t upperlimit);
int32_t hw_random(int32_t lowerlimit, int32_t upperlimit);
inline uint16_t hw_random16() { return static_cast<uint16_t>(hw_random()); }
inline uint16_t hw_random16(uint32_t upperlimit) { return (static_cast<uint32_t>(hw_random16()) * upperlimit) >> 16; }
inline int16_t hw_random16(int32_t lowerlimit, int32_t upperlimit) {
  const int32_t range = upperlimit - lowerlimit;
  return static_cast<int16_t>(lowerlimit + hw_random16(static_cast<uint32_t>(range)));
}
inline uint8_t hw_random8() { return static_cast<uint8_t>(hw_random()); }
inline uint8_t hw_random8(uint32_t upperlimit) { return static_cast<uint8_t>((static_cast<uint32_t>(hw_random8()) * upperlimit) >> 8); }
inline uint8_t hw_random8(uint32_t lowerlimit, uint32_t upperlimit) {
  const uint32_t range = upperlimit - lowerlimit;
  return static_cast<uint8_t>(lowerlimit + hw_random8(range));
}

uint8_t get_random_wheel_index(uint8_t pos);
uint32_t color_blend(uint32_t color1, uint32_t color2, uint8_t blend);
uint32_t color_fade(uint32_t color, uint8_t amount, bool video = false);
bool colorFromHexString(byte* rgb, const char* in);

#endif

#endif // WLED_HOST_CORE_H
