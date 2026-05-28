#pragma once

#include "../arduino_compat/IPAddress.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct NativeNetworkInterface {
  std::string name;
  IPAddress address;
  bool loopback = false;
  bool up = false;
};

class NativeIdentityStore {
public:
  explicit NativeIdentityStore(const std::string &configDir);

  std::string loadOrCreateMac();
  static bool isValidMac(const std::string &mac);

private:
  std::string _configDir;
  std::string _identityPath;

  std::string readPersistedMac() const;
  bool writePersistedMac(const std::string &mac) const;
  std::string generateMac() const;
};

bool resolveHostname(const std::string &hostname, IPAddress &address);
std::vector<NativeNetworkInterface> listNetworkInterfaces();

class NativeUdpSocket {
public:
  NativeUdpSocket() = default;
  ~NativeUdpSocket();

  NativeUdpSocket(const NativeUdpSocket &) = delete;
  NativeUdpSocket &operator=(const NativeUdpSocket &) = delete;

  bool bind(const IPAddress &address, uint16_t port);
  size_t sendTo(const uint8_t *data, size_t size, const IPAddress &address, uint16_t port) const;
  int receive(uint8_t *buffer, size_t size, IPAddress &remoteAddress, uint16_t &remotePort, int timeoutMs) const;
  uint16_t localPort() const;
  void close();

private:
  int _fd = -1;
};
