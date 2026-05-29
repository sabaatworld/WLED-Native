#ifndef ARDUINO

#include "wled_host_core.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

std::mt19937& hostRandomGenerator() {
  static std::mt19937 generator(std::random_device{}());
  return generator;
}

} // namespace

uint32_t hw_random() {
  return hostRandomGenerator()();
}

uint32_t hw_random(uint32_t upperlimit) {
  if (upperlimit == 0) return 0;
  const uint64_t scaled = static_cast<uint64_t>(hw_random()) * static_cast<uint64_t>(upperlimit);
  return static_cast<uint32_t>(scaled >> 32);
}

int32_t hw_random(int32_t lowerlimit, int32_t upperlimit) {
  if (lowerlimit >= upperlimit) return lowerlimit;
  const uint32_t diff = static_cast<uint32_t>(upperlimit - lowerlimit);
  return lowerlimit + static_cast<int32_t>(hw_random(diff));
}

uint8_t get_random_wheel_index(uint8_t pos) {
  uint8_t candidate = 0;
  uint8_t leftDistance = 0;
  uint8_t rightDistance = 0;
  uint8_t minDistance = 0;
  while (minDistance < 42) {
    candidate = hw_random8();
    leftDistance = static_cast<uint8_t>(std::abs(static_cast<int>(pos) - static_cast<int>(candidate)));
    rightDistance = static_cast<uint8_t>(255 - leftDistance);
    minDistance = std::min(leftDistance, rightDistance);
  }
  return candidate;
}

#endif // !ARDUINO
