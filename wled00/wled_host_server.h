#ifndef WLED_HOST_SERVER_H
#define WLED_HOST_SERVER_H

#ifndef ARDUINO

#include "wled_host_storage.h"

#include <atomic>
#include <string>

struct HostServerOptions {
  std::string host = "127.0.0.1";
  int port = 21324;
  std::string productName = "WLED";
  std::string version = "dev";
  std::string instanceId;
  HostStorageLayout storage;
};

class HostServer {
public:
  HostServer();
  ~HostServer();

  bool start(const HostServerOptions& options, std::string& error);
  bool inspectJson(const HostServerOptions& options, const std::string& target, std::string& output, std::string& error);
  bool renderRoute(const HostServerOptions& options, const std::string& path, std::string& contentType, std::string& output, std::string& error);
  bool applySettings(const HostServerOptions& options, const std::string& logicalPath, const std::string& encodedBody, std::string& error);
  bool applyJson(const HostServerOptions& options, const std::string& logicalPath, const std::string& body, std::string& output, std::string& error);
  bool stageUpdate(const HostServerOptions& options, const std::string& fileName, const std::string& body, std::string& error);
  int port() const;
  std::string listeningUrl() const;
  void runUntilStopped(const std::atomic<bool>& stopRequested);
  void stop();

private:
  struct Impl;
  Impl* impl_;
};

#endif

#endif // WLED_HOST_SERVER_H
