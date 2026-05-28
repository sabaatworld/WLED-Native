#pragma once

#include "../core/NativeWledCore.h"
#include "../output/RenderBuffer.h"

#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <string>
#include <thread>

struct NativeLightState {
  bool on = true;
  uint8_t bri = 128;
};

struct NativeHttpServerOptions {
  std::string host = "127.0.0.1";
  uint16_t port = 8080;
  std::string webRoot = "wled00/data";
  std::string version = "dev";
  std::string nativeMac = "020000000000";
  NativeRenderBuffer *renderBuffer = nullptr;
  NativeWledCore *core = nullptr;
};

class NativeHttpServer {
public:
  explicit NativeHttpServer(const NativeHttpServerOptions &options);
  ~NativeHttpServer();

  NativeHttpServer(const NativeHttpServer &) = delete;
  NativeHttpServer &operator=(const NativeHttpServer &) = delete;
  NativeHttpServer(NativeHttpServer &&other) noexcept;
  NativeHttpServer &operator=(NativeHttpServer &&other) noexcept;

  bool start();
  void stop();
  bool running() const;
  uint16_t localPort() const;
  const NativeLightState &state() const;

private:
  NativeHttpServerOptions _options;
  NativeRenderBuffer _fallbackRenderBuffer;
  NativeLightState _state;
  std::atomic<bool> _running{false};
  int _listenFd = -1;
  uint16_t _localPort = 0;
  uint64_t _startMs = 0;
  std::thread _acceptThread;
  mutable std::mutex _stateMutex;
  NativeWledCore _fallbackCore;

  void acceptLoop();
  void handleClient(int clientFd);
  void handleWebSocket(int clientFd, const std::string &request);
  std::string jsonState() const;
  std::string jsonInfo() const;
  std::string jsonStateInfo() const;
  void applyJsonState(const std::string &body);
  NativeRenderBuffer &renderBuffer();
  const NativeRenderBuffer &renderBuffer() const;
  NativeWledCore &core();
  const NativeWledCore &core() const;
};
