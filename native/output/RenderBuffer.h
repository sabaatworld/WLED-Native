#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct NativeColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t w = 0;
  uint16_t cct = 0;
};

enum class NativeColorOrder {
  RGB,
  GRB,
  BRG,
  RGBW,
  GRBW
};

class NativeRenderBuffer {
public:
  explicit NativeRenderBuffer(size_t length = 0);

  bool resize(size_t length);
  size_t length() const;
  void clear();
  void setPixel(size_t index, const NativeColor &color);
  NativeColor pixel(size_t index) const;
  const std::vector<NativeColor> &pixels() const;
  std::vector<uint8_t> encode(NativeColorOrder order) const;
  uint32_t estimatedCurrentMilliamps() const;
  bool hasVisiblePixels() const;

private:
  std::vector<NativeColor> _pixels;
};

class NativeOutputBackend {
public:
  virtual ~NativeOutputBackend() = default;

  virtual bool begin(size_t length) = 0;
  virtual bool show(const NativeRenderBuffer &buffer) = 0;
};

class NativeNullOutputBackend : public NativeOutputBackend {
public:
  bool begin(size_t length) override;
  bool show(const NativeRenderBuffer &buffer) override;

  uint64_t frameCount() const;
  const std::vector<NativeColor> &lastFrame() const;

private:
  size_t _length = 0;
  uint64_t _frameCount = 0;
  std::vector<NativeColor> _lastFrame;
};
