#ifndef WLED_HOST_USERMOD_H
#define WLED_HOST_USERMOD_H

#ifndef ARDUINO

#include "wled_host_runtime_state.h"
#include "src/dependencies/json/ArduinoJson-v6.h"

#include <cstdint>
#include <memory>
#include <vector>

constexpr uint16_t WLED_HOST_USERMOD_ID_UNSPECIFIED = 1;
constexpr uint16_t WLED_HOST_USERMOD_ID_AUTO_SAVE = 9;

class HostUsermodContext {
public:
  virtual ~HostUsermodContext() = default;
  virtual uint64_t millis() const = 0;
  virtual HostRuntimeState getState() const = 0;
  virtual bool applyPreset(uint8_t presetId) = 0;
  virtual bool savePreset(uint8_t presetId, const char* name) = 0;
};

class HostUsermod {
public:
  virtual ~HostUsermod() = default;
  virtual const char* name() const = 0;
  virtual uint16_t getId() const { return WLED_HOST_USERMOD_ID_UNSPECIFIED; }
  virtual void setup(HostUsermodContext&) {}
  virtual void loop(HostUsermodContext&) {}
  virtual void addToJsonState(HostUsermodContext&, JsonObject&) {}
  virtual void addToJsonInfo(HostUsermodContext&, JsonObject&) {}
  virtual void readFromJsonState(HostUsermodContext&, JsonObjectConst) {}
  virtual void addToConfig(JsonObject&) {}
  virtual bool readFromConfig(JsonObjectConst) { return true; }
  virtual void onStateChange(HostUsermodContext&) {}
};

class HostUsermodManager {
public:
  void registerBuiltins();
  void setup(HostUsermodContext& context);
  void loop(HostUsermodContext& context);
  void addToJsonState(HostUsermodContext& context, JsonObject& root);
  void addToJsonInfo(HostUsermodContext& context, JsonObject& root);
  void readFromJsonState(HostUsermodContext& context, JsonObjectConst root);
  void addToConfig(JsonObject& root);
  bool readFromConfig(JsonObjectConst root);
  void onStateChange(HostUsermodContext& context);
  size_t getCount() const { return usermods_.size(); }

private:
  std::vector<std::unique_ptr<HostUsermod>> usermods_;
  bool setupDone_ = false;
};

#endif

#endif // WLED_HOST_USERMOD_H
