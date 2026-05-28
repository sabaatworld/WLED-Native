#pragma once

#include "../http/NativeHttpServer.h"
#include "../core/NativeWledCore.h"
#include "../output/RenderBuffer.h"

#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>

constexpr int WLED_NATIVE_EXIT_OK = 0;
constexpr int WLED_NATIVE_EXIT_RESTART = 75;

struct NativeRuntimeOptions {
  std::string configDir;
  std::string host = "127.0.0.1";
  uint16_t port = 8080;
  std::string logLevel = "info";
  uint32_t loopDelayMs = 1;
  uint64_t maxLoopIterations = 0;
  uint64_t durationMs = 0;
};

class NativeRuntime {
public:
  explicit NativeRuntime(const NativeRuntimeOptions &options);

  int run();
  void requestStop();
  void requestRestart();

  bool stopRequested() const;
  bool restartRequested() const;
  uint64_t loopIterations() const;
  uint64_t uptimeMs() const;

private:
  NativeRuntimeOptions _options;
  std::atomic<bool> _stopRequested{false};
  std::atomic<bool> _restartRequested{false};
  std::atomic<uint64_t> _loopIterations{0};
  uint64_t _startMs = 0;
  NativeRenderBuffer _renderBuffer;
  NativeNullOutputBackend _outputBackend;
  NativeWledCore _core;
  std::unique_ptr<NativeHttpServer> _httpServer;

  void setup();
  void loopOnce();
  void shutdown();
};

struct NativeCliResult {
  bool ok = false;
  bool exitNow = false;
  int exitCode = WLED_NATIVE_EXIT_OK;
  NativeRuntimeOptions options;
};

bool parseNativeCli(int argc, char **argv, NativeCliResult &result, std::ostream &out, std::ostream &err);
void printNativeHelp(std::ostream &out);
void printNativeVersion(std::ostream &out);
