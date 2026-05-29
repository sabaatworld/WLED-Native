#ifndef ARDUINO

#include "wled_host_server.h"

#include "wled_host_storage.h"
#include "wled_host_playlist.h"
#include "wled_host_presets.h"
#include "wled_host_runtime_state.h"
#include "wled_host_usermod.h"
#include "src/dependencies/json/ArduinoJson-v6.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef WLED_HOST_REPO_ROOT
  #define WLED_HOST_REPO_ROOT "."
#endif

namespace {

constexpr size_t kMaxRequestBytes = 1024 * 1024;
constexpr size_t kHttpReadChunk = 4096;
constexpr uint8_t kHostCustomPaletteIdBase = 200;
constexpr size_t kHostMaxCustomPalettes = 129;
constexpr unsigned kHostMaxCustomPaletteGap = 20;

struct HttpRequest {
  std::string method;
  std::string path;
  std::map<std::string, std::string> headers;
  std::string body;
};

using QueryParams = std::map<std::string, std::string>;
using FormFields = std::map<std::string, std::string>;

struct HostPersistedConfig {
  struct MatrixPanelConfig {
    uint8_t bottomStart = 0;
    uint8_t rightStart = 0;
    uint8_t vertical = 0;
    bool serpentine = false;
    uint16_t xOffset = 0;
    uint16_t yOffset = 0;
    uint16_t width = 8;
    uint16_t height = 8;
  };

  std::string deviceName = "WLED Native";
  bool simplifiedUi = false;

  std::string mdnsName = "wled-native";
  std::string apSsid = "WLED";
  uint8_t apChannel = 1;
  std::array<uint8_t, 4> dns = {0, 0, 0, 0};

  uint16_t ledCount = 30;
  uint16_t transition = 7;
  uint8_t brightness = 128;
  uint8_t bootPreset = 0;

  uint16_t udpPort = 21324;
  uint16_t udpPort2 = 65506;
  uint8_t udpRetries = 0;
  uint16_t realtimePort = 5568;
  uint16_t realtimeUniverse = 1;
  uint16_t dmxAddress = 1;
  uint16_t dmxSpacing = 0;
  uint8_t e131Priority = 100;
  uint8_t dmxMode = 0;
  uint16_t realtimeTimeoutMs = 2500;
  int16_t realtimeOffset = 0;
  bool nodeListEnabled = true;
  bool nodeBroadcastEnabled = true;

  std::string ntpServer = "pool.ntp.org";
  uint8_t timezone = 0;
  long utcOffsetSeconds = 0;
  std::string longitude = "0";
  std::string latitude = "0";

  std::string settingsPin;
  bool otaLock = true;
  bool wifiLock = false;
  bool arduinoOta = false;
  bool otaSameSubnet = true;

  uint16_t dmxProxyUniverse = 0;
  uint8_t dmxChannels = 7;
  uint16_t dmxGap = 10;
  uint16_t dmxStart = 10;
  uint16_t dmxStartLed = 0;
  std::array<uint8_t, 15> dmxFixtureMap = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  bool matrixEnabled = false;
  std::vector<MatrixPanelConfig> matrixPanels;
};

struct HostEffectCatalogEntry {
  std::string name;
  std::string data;
};

struct HostCustomPaletteEntry {
  bool placeholder = false;
  std::vector<std::array<uint8_t, 4>> stops;
};

struct HostDiscoveredNode {
  std::string instanceId;
  std::string name;
  std::string ip;
  int port = 0;
  uint64_t updatedAtMs = 0;
  uint8_t type = 128;
  uint32_t vid = 1700000;
};

struct StaticResponse {
  int status = 200;
  std::string contentType;
  std::string body;
};

struct WsClient {
  int fd = -1;
  std::mutex writeMutex;
};

std::string trim(const std::string& value) {
  const size_t start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string jsonStringLiteral(const std::string& value) {
  std::ostringstream output;
  output << '"';
  for (const char c : value) {
    switch (c) {
      case '\\': output << "\\\\"; break;
      case '"': output << "\\\""; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default: output << c; break;
    }
  }
  output << '"';
  return output.str();
}

bool sendAll(int fd, const void* buffer, size_t length) {
  const char* data = static_cast<const char*>(buffer);
  size_t offset = 0;
  while (offset < length) {
    const ssize_t written = send(fd, data + offset, length - offset, 0);
    if (written <= 0) return false;
    offset += static_cast<size_t>(written);
  }
  return true;
}

bool sendString(int fd, const std::string& body) {
  return sendAll(fd, body.data(), body.size());
}

std::string mimeTypeForPath(const std::filesystem::path& path) {
  const std::string ext = toLower(path.extension().string());
  if (ext == ".htm" || ext == ".html") return "text/html; charset=utf-8";
  if (ext == ".js") return "application/javascript; charset=utf-8";
  if (ext == ".css") return "text/css; charset=utf-8";
  if (ext == ".json") return "application/json; charset=utf-8";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".png") return "image/png";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".gif") return "image/gif";
  if (ext == ".ico") return "image/x-icon";
  return "application/octet-stream";
}

// AI: below section was generated by an AI
std::string jsStringLiteral(const std::string& value) {
  std::ostringstream output;
  output << '"';
  for (const char c : value) {
    switch (c) {
      case '\\': output << "\\\\"; break;
      case '"': output << "\\\""; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default: output << c; break;
    }
  }
  output << '"';
  return output.str();
}

std::string reasonPhrase(int status) {
  switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    default: return "OK";
  }
}

StaticResponse makeJsonResponse(const std::string& body) {
  return {200, "application/json; charset=utf-8", body};
}

StaticResponse makeTextResponse(int status, const std::string& body) {
  return {status, "text/plain; charset=utf-8", body};
}

bool writeHttpResponse(int fd, const StaticResponse& response) {
  std::ostringstream header;
  header
    << "HTTP/1.1 " << response.status << ' ' << reasonPhrase(response.status) << "\r\n"
    << "Content-Type: " << response.contentType << "\r\n"
    << "Content-Length: " << response.body.size() << "\r\n"
    << "Cache-Control: no-store\r\n"
    << "Connection: close\r\n"
    << "\r\n";
  return sendString(fd, header.str()) && sendString(fd, response.body);
}

bool readHttpRequest(int fd, HttpRequest& request, std::string& error) {
  std::string buffer;
  std::array<char, kHttpReadChunk> chunk{};
  size_t headerEnd = std::string::npos;

  while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
    const ssize_t count = recv(fd, chunk.data(), chunk.size(), 0);
    if (count <= 0) {
      error = "Unable to read HTTP request";
      return false;
    }
    buffer.append(chunk.data(), static_cast<size_t>(count));
    if (buffer.size() > kMaxRequestBytes) {
      error = "HTTP request exceeds the maximum supported size";
      return false;
    }
  }

  std::istringstream stream(buffer.substr(0, headerEnd));
  std::string requestLine;
  if (!std::getline(stream, requestLine)) {
    error = "HTTP request line is missing";
    return false;
  }
  if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

  std::istringstream lineStream(requestLine);
  std::string version;
  if (!(lineStream >> request.method >> request.path >> version)) {
    error = "HTTP request line is invalid";
    return false;
  }

  std::string headerLine;
  while (std::getline(stream, headerLine)) {
    if (!headerLine.empty() && headerLine.back() == '\r') headerLine.pop_back();
    const size_t separator = headerLine.find(':');
    if (separator == std::string::npos) continue;
    request.headers.emplace(toLower(trim(headerLine.substr(0, separator))), trim(headerLine.substr(separator + 1)));
  }

  size_t contentLength = 0;
  if (const auto header = request.headers.find("content-length"); header != request.headers.end()) {
    contentLength = static_cast<size_t>(std::strtoull(header->second.c_str(), nullptr, 10));
  }

  request.body = buffer.substr(headerEnd + 4);
  while (request.body.size() < contentLength) {
    const ssize_t count = recv(fd, chunk.data(), chunk.size(), 0);
    if (count <= 0) {
      error = "HTTP request body is incomplete";
      return false;
    }
    request.body.append(chunk.data(), static_cast<size_t>(count));
    if (request.body.size() > kMaxRequestBytes) {
      error = "HTTP request body exceeds the maximum supported size";
      return false;
    }
  }

  if (request.body.size() > contentLength) request.body.resize(contentLength);
  return true;
}

// AI: below section was generated by an AI
void parseUint8Field(const FormFields& formFields, const char* key, uint8_t& target) {
  if (const auto field = formFields.find(key); field != formFields.end()) target = static_cast<uint8_t>(std::strtoul(field->second.c_str(), nullptr, 10));
}

void parseUint16Field(const FormFields& formFields, const char* key, uint16_t& target) {
  if (const auto field = formFields.find(key); field != formFields.end()) target = static_cast<uint16_t>(std::strtoul(field->second.c_str(), nullptr, 10));
}

void parseInt16Field(const FormFields& formFields, const char* key, int16_t& target) {
  if (const auto field = formFields.find(key); field != formFields.end()) target = static_cast<int16_t>(std::strtol(field->second.c_str(), nullptr, 10));
}

void parseStringField(const FormFields& formFields, const char* key, std::string& target) {
  if (const auto field = formFields.find(key); field != formFields.end()) target = field->second;
}

void parseCheckboxField(const FormFields& formFields, const char* key, bool& target) {
  target = formFields.find(key) != formFields.end();
}

std::string urlDecode(std::string value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    const char current = value[index];
    if (current == '+') {
      decoded.push_back(' ');
      continue;
    }
    if (current == '%' && index + 2 < value.size() &&
        std::isxdigit(static_cast<unsigned char>(value[index + 1])) &&
        std::isxdigit(static_cast<unsigned char>(value[index + 2]))) {
      const std::string hex = value.substr(index + 1, 2);
      decoded.push_back(static_cast<char>(std::strtoul(hex.c_str(), nullptr, 16)));
      index += 2;
      continue;
    }
    decoded.push_back(current);
  }
  return decoded;
}

FormFields parseFormUrlEncoded(const std::string& body) {
  FormFields fields;
  size_t offset = 0;
  while (offset <= body.size()) {
    const size_t separator = body.find('&', offset);
    const std::string token = body.substr(offset, separator == std::string::npos ? std::string::npos : separator - offset);
    if (!token.empty()) {
      const size_t equals = token.find('=');
      const std::string key = urlDecode(token.substr(0, equals));
      const std::string value = equals == std::string::npos ? "" : urlDecode(token.substr(equals + 1));
      if (!key.empty()) fields[key] = value;
    }
    if (separator == std::string::npos) break;
    offset = separator + 1;
  }
  return fields;
}

bool parseBoolValue(const std::string& value) {
  std::string normalized = value;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return normalized == "1" || normalized == "true" || normalized == "on" || normalized == "yes";
}

bool parseIntegerValue(const std::string& value, long long& parsedValue) {
  if (value.empty()) return false;
  char* end = nullptr;
  parsedValue = std::strtoll(value.c_str(), &end, 10);
  return end && *end == '\0';
}

bool parseFloatingPointValue(const std::string& value, double& parsedValue) {
  if (value.empty()) return false;
  char* end = nullptr;
  parsedValue = std::strtod(value.c_str(), &end);
  return end && *end == '\0';
}

void mergeMissingJsonObject(JsonObject target, JsonObjectConst defaults) {
  for (JsonPairConst pair : defaults) {
    JsonVariant existing = target[pair.key().c_str()];
    if (existing.isNull()) {
      target[pair.key().c_str()] = pair.value();
      continue;
    }

    if (existing.is<JsonObject>() && pair.value().is<JsonObjectConst>()) {
      mergeMissingJsonObject(existing.as<JsonObject>(), pair.value().as<JsonObjectConst>());
    }
  }
}

void applyTypedFormField(JsonVariant target, const std::string& rawValue) {
  if (target.is<bool>()) {
    target.set(parseBoolValue(rawValue));
    return;
  }

  if (target.is<const char*>() || target.is<std::string>()) {
    target.set(rawValue);
    return;
  }

  long long integerValue = 0;
  if (parseIntegerValue(rawValue, integerValue)) {
    target.set(integerValue);
    return;
  }

  double floatingValue = 0.0;
  if (parseFloatingPointValue(rawValue, floatingValue)) {
    target.set(floatingValue);
    return;
  }

  target.set(rawValue);
}

void applyFormFieldsToJsonObject(JsonObject target, const std::string& prefix, const FormFields& formFields) {
  for (JsonPair pair : target) {
    const std::string key = prefix.empty() ? pair.key().c_str() : prefix + ":" + pair.key().c_str();
    JsonVariant value = pair.value();
    if (value.is<JsonObject>()) {
      applyFormFieldsToJsonObject(value.as<JsonObject>(), key, formFields);
      continue;
    }
    if (value.is<JsonArray>()) continue;

    const auto field = formFields.find(key);
    if (field == formFields.end()) continue;
    applyTypedFormField(value, field->second);
  }
}

void loadConfigSnapshotFromJson(JsonObjectConst root, HostPersistedConfig& config) {
  if (root["id"]["name"].is<const char*>()) config.deviceName = root["id"]["name"].as<const char*>();
  if (root["ui"]["simplified"].is<bool>()) config.simplifiedUi = root["ui"]["simplified"].as<bool>();
  if (root["host"]["wifi"]["mdns"].is<const char*>()) config.mdnsName = root["host"]["wifi"]["mdns"].as<const char*>();
  if (root["host"]["wifi"]["apSsid"].is<const char*>()) config.apSsid = root["host"]["wifi"]["apSsid"].as<const char*>();
  if (root["host"]["wifi"]["apChannel"].is<uint8_t>()) config.apChannel = root["host"]["wifi"]["apChannel"].as<uint8_t>();

  JsonArrayConst dns = root["host"]["wifi"]["dns"].as<JsonArrayConst>();
  for (size_t index = 0; index < dns.size() && index < config.dns.size(); ++index) {
    if (dns[index].is<uint8_t>()) config.dns[index] = dns[index].as<uint8_t>();
  }

  if (root["host"]["led"]["count"].is<uint16_t>()) config.ledCount = root["host"]["led"]["count"].as<uint16_t>();
  if (root["host"]["led"]["transition"].is<uint16_t>()) config.transition = root["host"]["led"]["transition"].as<uint16_t>();
  if (root["host"]["led"]["brightness"].is<uint8_t>()) config.brightness = root["host"]["led"]["brightness"].as<uint8_t>();
  if (root["def"]["ps"].is<uint8_t>()) config.bootPreset = root["def"]["ps"].as<uint8_t>();

  if (root["host"]["sync"]["udpPort"].is<uint16_t>()) config.udpPort = root["host"]["sync"]["udpPort"].as<uint16_t>();
  if (root["host"]["sync"]["udpPort2"].is<uint16_t>()) config.udpPort2 = root["host"]["sync"]["udpPort2"].as<uint16_t>();
  if (root["host"]["sync"]["udpRetries"].is<uint8_t>()) config.udpRetries = root["host"]["sync"]["udpRetries"].as<uint8_t>();
  if (root["host"]["sync"]["realtimePort"].is<uint16_t>()) config.realtimePort = root["host"]["sync"]["realtimePort"].as<uint16_t>();
  if (root["host"]["sync"]["realtimeUniverse"].is<uint16_t>()) config.realtimeUniverse = root["host"]["sync"]["realtimeUniverse"].as<uint16_t>();
  if (root["host"]["sync"]["dmxAddress"].is<uint16_t>()) config.dmxAddress = root["host"]["sync"]["dmxAddress"].as<uint16_t>();
  if (root["host"]["sync"]["dmxSpacing"].is<uint16_t>()) config.dmxSpacing = root["host"]["sync"]["dmxSpacing"].as<uint16_t>();
  if (root["host"]["sync"]["e131Priority"].is<uint8_t>()) config.e131Priority = root["host"]["sync"]["e131Priority"].as<uint8_t>();
  if (root["host"]["sync"]["dmxMode"].is<uint8_t>()) config.dmxMode = root["host"]["sync"]["dmxMode"].as<uint8_t>();
  if (root["host"]["sync"]["realtimeTimeoutMs"].is<uint16_t>()) config.realtimeTimeoutMs = root["host"]["sync"]["realtimeTimeoutMs"].as<uint16_t>();
  if (root["host"]["sync"]["realtimeOffset"].is<int>()) config.realtimeOffset = static_cast<int16_t>(root["host"]["sync"]["realtimeOffset"].as<int>());
  if (root["host"]["sync"]["nodeListEnabled"].is<bool>()) config.nodeListEnabled = root["host"]["sync"]["nodeListEnabled"].as<bool>();
  if (root["host"]["sync"]["nodeBroadcastEnabled"].is<bool>()) config.nodeBroadcastEnabled = root["host"]["sync"]["nodeBroadcastEnabled"].as<bool>();

  if (root["host"]["time"]["ntpServer"].is<const char*>()) config.ntpServer = root["host"]["time"]["ntpServer"].as<const char*>();
  if (root["host"]["time"]["timezone"].is<uint8_t>()) config.timezone = root["host"]["time"]["timezone"].as<uint8_t>();
  if (root["host"]["time"]["utcOffsetSeconds"].is<long>()) config.utcOffsetSeconds = root["host"]["time"]["utcOffsetSeconds"].as<long>();
  if (root["host"]["time"]["longitude"].is<const char*>()) config.longitude = root["host"]["time"]["longitude"].as<const char*>();
  if (root["host"]["time"]["latitude"].is<const char*>()) config.latitude = root["host"]["time"]["latitude"].as<const char*>();

  if (root["host"]["security"]["pin"].is<const char*>()) config.settingsPin = root["host"]["security"]["pin"].as<const char*>();
  if (root["host"]["security"]["otaLock"].is<bool>()) config.otaLock = root["host"]["security"]["otaLock"].as<bool>();
  if (root["host"]["security"]["wifiLock"].is<bool>()) config.wifiLock = root["host"]["security"]["wifiLock"].as<bool>();
  if (root["host"]["security"]["arduinoOta"].is<bool>()) config.arduinoOta = root["host"]["security"]["arduinoOta"].as<bool>();
  if (root["host"]["security"]["otaSameSubnet"].is<bool>()) config.otaSameSubnet = root["host"]["security"]["otaSameSubnet"].as<bool>();

  if (root["host"]["dmx"]["proxyUniverse"].is<uint16_t>()) config.dmxProxyUniverse = root["host"]["dmx"]["proxyUniverse"].as<uint16_t>();
  if (root["host"]["dmx"]["channelsPerFixture"].is<uint8_t>()) config.dmxChannels = root["host"]["dmx"]["channelsPerFixture"].as<uint8_t>();
  if (root["host"]["dmx"]["fixtureSpacing"].is<uint16_t>()) config.dmxGap = root["host"]["dmx"]["fixtureSpacing"].as<uint16_t>();
  if (root["host"]["dmx"]["fixtureStartAddress"].is<uint16_t>()) config.dmxStart = root["host"]["dmx"]["fixtureStartAddress"].as<uint16_t>();
  if (root["host"]["dmx"]["startLed"].is<uint16_t>()) config.dmxStartLed = root["host"]["dmx"]["startLed"].as<uint16_t>();
  JsonArrayConst fixtureMap = root["host"]["dmx"]["fixtureMap"].as<JsonArrayConst>();
  for (size_t index = 0; index < config.dmxFixtureMap.size() && index < fixtureMap.size(); ++index) {
    if (fixtureMap[index].is<uint8_t>()) config.dmxFixtureMap[index] = fixtureMap[index].as<uint8_t>();
  }

  config.matrixEnabled = root["host"]["matrix"]["enabled"] | config.matrixEnabled;
  config.matrixPanels.clear();
  JsonArrayConst matrixPanels = root["host"]["matrix"]["panels"].as<JsonArrayConst>();
  for (JsonObjectConst panelObject : matrixPanels) {
    HostPersistedConfig::MatrixPanelConfig panel;
    panel.bottomStart = panelObject["bottomStart"] | panel.bottomStart;
    panel.rightStart = panelObject["rightStart"] | panel.rightStart;
    panel.vertical = panelObject["vertical"] | panel.vertical;
    panel.serpentine = panelObject["serpentine"] | panel.serpentine;
    panel.xOffset = panelObject["xOffset"] | panel.xOffset;
    panel.yOffset = panelObject["yOffset"] | panel.yOffset;
    panel.width = panelObject["width"] | panel.width;
    panel.height = panelObject["height"] | panel.height;
    config.matrixPanels.push_back(panel);
  }
}

void writeConfigSnapshotToJson(const HostPersistedConfig& config, JsonObject root) {
  root["id"]["name"] = config.deviceName.c_str();
  root["ui"]["simplified"] = config.simplifiedUi;

  root["host"]["wifi"]["mdns"] = config.mdnsName.c_str();
  root["host"]["wifi"]["apSsid"] = config.apSsid.c_str();
  root["host"]["wifi"]["apChannel"] = config.apChannel;
  JsonArray dns = root["host"]["wifi"].containsKey("dns") ? root["host"]["wifi"]["dns"].as<JsonArray>() : root["host"]["wifi"].createNestedArray("dns");
  dns.clear();
  for (const uint8_t octet : config.dns) dns.add(octet);

  root["host"]["led"]["count"] = config.ledCount;
  root["host"]["led"]["transition"] = config.transition;
  root["host"]["led"]["brightness"] = config.brightness;
  root["def"]["ps"] = config.bootPreset;

  root["host"]["sync"]["udpPort"] = config.udpPort;
  root["host"]["sync"]["udpPort2"] = config.udpPort2;
  root["host"]["sync"]["udpRetries"] = config.udpRetries;
  root["host"]["sync"]["realtimePort"] = config.realtimePort;
  root["host"]["sync"]["realtimeUniverse"] = config.realtimeUniverse;
  root["host"]["sync"]["dmxAddress"] = config.dmxAddress;
  root["host"]["sync"]["dmxSpacing"] = config.dmxSpacing;
  root["host"]["sync"]["e131Priority"] = config.e131Priority;
  root["host"]["sync"]["dmxMode"] = config.dmxMode;
  root["host"]["sync"]["realtimeTimeoutMs"] = config.realtimeTimeoutMs;
  root["host"]["sync"]["realtimeOffset"] = config.realtimeOffset;
  root["host"]["sync"]["nodeListEnabled"] = config.nodeListEnabled;
  root["host"]["sync"]["nodeBroadcastEnabled"] = config.nodeBroadcastEnabled;

  root["host"]["time"]["ntpServer"] = config.ntpServer.c_str();
  root["host"]["time"]["timezone"] = config.timezone;
  root["host"]["time"]["utcOffsetSeconds"] = config.utcOffsetSeconds;
  root["host"]["time"]["longitude"] = config.longitude.c_str();
  root["host"]["time"]["latitude"] = config.latitude.c_str();

  root["host"]["security"]["pin"] = config.settingsPin.c_str();
  root["host"]["security"]["otaLock"] = config.otaLock;
  root["host"]["security"]["wifiLock"] = config.wifiLock;
  root["host"]["security"]["arduinoOta"] = config.arduinoOta;
  root["host"]["security"]["otaSameSubnet"] = config.otaSameSubnet;

  root["host"]["dmx"]["proxyUniverse"] = config.dmxProxyUniverse;
  root["host"]["dmx"]["channelsPerFixture"] = config.dmxChannels;
  root["host"]["dmx"]["fixtureSpacing"] = config.dmxGap;
  root["host"]["dmx"]["fixtureStartAddress"] = config.dmxStart;
  root["host"]["dmx"]["startLed"] = config.dmxStartLed;
  JsonArray fixtureMap = root["host"]["dmx"].createNestedArray("fixtureMap");
  for (uint8_t value : config.dmxFixtureMap) fixtureMap.add(value);

  root["host"]["matrix"]["enabled"] = config.matrixEnabled;
  JsonArray matrixPanels = root["host"]["matrix"].createNestedArray("panels");
  for (const HostPersistedConfig::MatrixPanelConfig& panel : config.matrixPanels) {
    JsonObject panelObject = matrixPanels.createNestedObject();
    panelObject["bottomStart"] = panel.bottomStart;
    panelObject["rightStart"] = panel.rightStart;
    panelObject["vertical"] = panel.vertical;
    panelObject["serpentine"] = panel.serpentine;
    panelObject["xOffset"] = panel.xOffset;
    panelObject["yOffset"] = panel.yOffset;
    panelObject["width"] = panel.width;
    panelObject["height"] = panel.height;
  }
}
// AI: end

std::filesystem::path repoDataRoot() {
  return std::filesystem::path(WLED_HOST_REPO_ROOT) / "wled00" / "data";
}

std::filesystem::path repoSourcePath(const char* relativePath) {
  return std::filesystem::path(WLED_HOST_REPO_ROOT) / relativePath;
}

std::string extractEffectName(const std::string& effectData) {
  const size_t separator = effectData.find('@');
  return effectData.substr(0, separator == std::string::npos ? effectData.size() : separator);
}

std::string unescapeCppString(std::string value) {
  std::string output;
  output.reserve(value.size());
  bool escaped = false;
  for (const char current : value) {
    if (escaped) {
      switch (current) {
        case '\\':
        case '"':
          output.push_back(current);
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        default:
          output.push_back(current);
          break;
      }
      escaped = false;
      continue;
    }
    if (current == '\\') {
      escaped = true;
      continue;
    }
    output.push_back(current);
  }
  if (escaped) output.push_back('\\');
  return output;
}

bool loadTextFile(const std::filesystem::path& path, std::string& output) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) return false;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  output = buffer.str();
  return true;
}

void splitPathAndQuery(const std::string& requestPath, std::string& logicalPath, QueryParams& queryParams) {
  logicalPath = requestPath;
  const size_t querySeparator = logicalPath.find('?');
  if (querySeparator == std::string::npos) return;

  const std::string query = logicalPath.substr(querySeparator + 1);
  logicalPath.resize(querySeparator);

  size_t offset = 0;
  while (offset <= query.size()) {
    const size_t separator = query.find('&', offset);
    const std::string token = query.substr(offset, separator == std::string::npos ? std::string::npos : separator - offset);
    if (!token.empty()) {
      const size_t equals = token.find('=');
      if (equals == std::string::npos) {
        queryParams.emplace(token, "");
      } else {
        queryParams.emplace(token.substr(0, equals), token.substr(equals + 1));
      }
    }
    if (separator == std::string::npos) break;
    offset = separator + 1;
  }
}

std::string normalizeStaticRoute(const std::string& requestPath) {
  if (requestPath.empty() || requestPath == "/") return "/index.htm";

  static const std::map<std::string, std::string> kRouteAliases = {
    {"/settings", "/settings.htm"},
    {"/settings/wifi", "/settings_wifi.htm"},
    {"/settings/leds", "/settings_leds.htm"},
    {"/settings/pins", "/settings_pininfo.htm"},
    {"/settings/2D", "/settings_2D.htm"},
    {"/settings/ui", "/settings_ui.htm"},
    {"/settings/dmx", "/settings_dmx.htm"},
    {"/settings/sync", "/settings_sync.htm"},
    {"/settings/time", "/settings_time.htm"},
    {"/settings/um", "/settings_um.htm"},
    {"/settings/sec", "/settings_sec.htm"},
    {"/welcome", "/welcome.htm"},
    {"/update", "/update.htm"},
    {"/edit", "/edit.htm"},
    {"/dmxmap", "/dmxmap.htm"},
    {"/liveview", "/liveview.htm"},
    {"/cpal.htm", "/cpal/cpal.htm"},
    {"/pixelforge.htm", "/pixelforge/pixelforge.htm"},
    {"/pxmagic.htm", "/pxmagic/pxmagic.htm"},
    {"/pixart.htm", "/pixart/pixart.htm"},
  };

  if (const auto alias = kRouteAliases.find(requestPath); alias != kRouteAliases.end()) return alias->second;
  if (requestPath.rfind("/settings/", 0) == 0) {
    const std::string leafName = requestPath.substr(std::string("/settings/").size());
    if (leafName == "common.js" || leafName == "style.css") return "/" + leafName;
  }
  return requestPath;
}

bool resolveStaticPath(const std::string& requestPath, std::filesystem::path& resolvedPath) {
  std::string logicalPath = normalizeStaticRoute(requestPath);

  if (!logicalPath.empty() && logicalPath.front() == '/') logicalPath.erase(logicalPath.begin());
  if (logicalPath.empty()) return false;

  const std::filesystem::path root = repoDataRoot();
  const std::filesystem::path candidate = std::filesystem::weakly_canonical(root / logicalPath);
  const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root);
  const std::string rootText = canonicalRoot.native();
  const std::string candidateText = candidate.native();
  if (candidateText.compare(0, rootText.size(), rootText) != 0) return false;
  if (!std::filesystem::is_regular_file(candidate)) return false;
  resolvedPath = candidate;
  return true;
}

bool resolveStorageAssetPath(const HostStorageLayout& storage, const std::string& requestPath, std::filesystem::path& resolvedPath) {
  std::string logicalPath = requestPath;
  while (!logicalPath.empty() && logicalPath.front() == '/') logicalPath.erase(logicalPath.begin());
  if (logicalPath.empty()) return false;

  std::string error;
  if (!resolveHostStoragePath(storage, logicalPath, resolvedPath, error)) return false;
  return std::filesystem::is_regular_file(resolvedPath);
}

bool readStaticFile(const std::filesystem::path& path, std::string& body) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) return false;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  body = buffer.str();
  return true;
}

uint64_t recursiveFileBytes(const std::filesystem::path& root) {
  std::error_code error;
  if (!std::filesystem::exists(root, error)) return 0;

  uint64_t total = 0;
  std::filesystem::recursive_directory_iterator iter(root, std::filesystem::directory_options::skip_permission_denied, error);
  std::filesystem::recursive_directory_iterator end;
  while (!error && iter != end) {
    if (iter->is_regular_file(error) && !error) total += static_cast<uint64_t>(iter->file_size(error));
    iter.increment(error);
  }
  return total;
}

uint64_t filesystemCapacityBytes(const std::filesystem::path& root) {
  std::error_code error;
  const std::filesystem::space_info info = std::filesystem::space(root, error);
  if (error) return 0;
  return static_cast<uint64_t>(info.capacity);
}

uint64_t fileModifiedTimeSeconds(const std::filesystem::path& path) {
  std::error_code error;
  const std::filesystem::file_time_type modified = std::filesystem::last_write_time(path, error);
  if (error) return 0;
  const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
    modified - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count());
}

std::string formatLocalTimeString() {
  const std::time_t now = std::time(nullptr);
  std::tm localTime {};
  localtime_r(&now, &localTime);
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime) == 0) return "";
  return buffer;
}

constexpr const char* kPendingUpdateFile = "/pending-update.bin";
constexpr const char* kPendingUpdateMetaFile = "/pending-update.json";

void appendSetValue(std::ostream& output, const std::string& fieldName, const std::string& value) {
  output << "if(d.Sf&&d.Sf[" << jsStringLiteral(fieldName) << "])d.Sf[" << jsStringLiteral(fieldName) << "].value=" << jsStringLiteral(value) << ';';
}

void appendSetCheckbox(std::ostream& output, const std::string& fieldName, bool checked) {
  output << "if(d.Sf&&d.Sf[" << jsStringLiteral(fieldName) << "])d.Sf[" << jsStringLiteral(fieldName) << "].checked=" << (checked ? "true" : "false") << ';';
}

void appendSetClassText(std::ostream& output, const std::string& className, size_t index, const std::string& value) {
  output << "{var e=d.getElementsByClassName(" << jsStringLiteral(className) << ")[" << index << "];if(e)e.textContent=" << jsStringLiteral(value) << ";}";
}

std::string buildDmxMapVarsScript(const HostPersistedConfig& config) {
  std::ostringstream output;
  output << "var CN=" << static_cast<unsigned>(config.dmxChannels) << ';'
         << "CS=" << config.dmxStart << ';'
         << "CG=" << config.dmxGap << ';'
         << "LC=" << config.ledCount << ';'
         << "var CH=[";
  for (uint8_t value : config.dmxFixtureMap) output << static_cast<unsigned>(value) << ',';
  output << "0];";
  return output.str();
}

std::string trimWhitespace(const std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
  return value.substr(start, end - start);
}

std::string runCommandCapture(const char* command) {
  std::array<char, 512> buffer {};
  std::string output;
  FILE* pipe = popen(command, "r");
  if (!pipe) return output;
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) output.append(buffer.data());
  pclose(pipe);
  return output;
}

std::filesystem::path nativeNodeRegistryPath() {
  if (const char* overridePath = std::getenv("WLED_NATIVE_NODE_REGISTRY_PATH"); overridePath && *overridePath) {
    return std::filesystem::path(overridePath);
  }
  return std::filesystem::temp_directory_path() / "wled-native-nodes.json";
}

std::string buildSettingsScript(int subPage, const HostPersistedConfig& config, const HostServerOptions& options) {
  std::ostringstream output;
  output << "function GetV(){var d=document;";
  output << "window.sd=" << jsStringLiteral(options.productName) << ';';

  switch (subPage) {
    case 0:
      output << "if(gId('dmxbtn'))gId('dmxbtn').style.display='none';";
      break;
    case 1:
      output << "if(typeof resetWiFi==='function')resetWiFi(1);";
      output << "if(typeof addWiFi==='function')addWiFi(\"\",\"\",\"\",0,0,16777215,0,\"\",\"\");";
      appendSetValue(output, "CM", config.mdnsName);
      appendSetValue(output, "AS", config.apSsid);
      appendSetValue(output, "AC", std::to_string(config.apChannel));
      appendSetValue(output, "D0", std::to_string(config.dns[0]));
      appendSetValue(output, "D1", std::to_string(config.dns[1]));
      appendSetValue(output, "D2", std::to_string(config.dns[2]));
      appendSetValue(output, "D3", std::to_string(config.dns[3]));
      output << "if(gId('ethd'))gId('ethd').style.display='none';";
      appendSetClassText(output, "sip", 0, "Host networking");
      appendSetClassText(output, "sip", 1, "Not active");
      break;
    case 2:
      output << "d.um_p=[-1];d.rsvd=[];d.ro_gpio=[];d.touch=[];d.adc=[];d.max_gpio=64;";
      output << "d.ledTypes=[{i:22,c:1,t:\"D\",n:\"WS2812B\"},{i:80,c:1,t:\"V\",n:\"Screen Preview\"}];";
      output << "if(typeof bLimits==='function')bLimits(4,2048,131072,8192,5,16,0,0,0,4);";
      output << "if(typeof addLEDs==='function'&&gEBCN('iST').length===0)addLEDs(1);";
      appendSetValue(output, "LC0", std::to_string(config.ledCount));
      appendSetValue(output, "LS0", "0");
      appendSetValue(output, "LT0", "80");
      appendSetValue(output, "CO0", "0");
      appendSetValue(output, "LD0", "0");
      appendSetValue(output, "SL0", "0");
      appendSetValue(output, "LA0", "55");
      appendSetValue(output, "MA0", "0");
      appendSetValue(output, "FR", "60");
      appendSetValue(output, "CA", std::to_string(config.brightness));
      appendSetValue(output, "TD", std::to_string(config.transition));
      output << "if(typeof resetCOM==='function')resetCOM(5);";
      output << "if(typeof addBtn==='function'&&gId('btns')&&gId('btns').children.length===0)addBtn(0,-1,0);";
      break;
    case 3:
      appendSetValue(output, "DS", config.deviceName);
      appendSetCheckbox(output, "SU", config.simplifiedUi);
      break;
    case 4:
      appendSetValue(output, "UP", std::to_string(config.udpPort));
      appendSetValue(output, "U2", std::to_string(config.udpPort2));
      appendSetValue(output, "UR", std::to_string(config.udpRetries));
      appendSetValue(output, "EP", std::to_string(config.realtimePort));
      appendSetValue(output, "EU", std::to_string(config.realtimeUniverse));
      appendSetValue(output, "DA", std::to_string(config.dmxAddress));
      appendSetValue(output, "XX", std::to_string(config.dmxSpacing));
      appendSetValue(output, "PY", std::to_string(config.e131Priority));
      appendSetValue(output, "DM", std::to_string(config.dmxMode));
      appendSetValue(output, "ET", std::to_string(config.realtimeTimeoutMs));
      appendSetValue(output, "WO", std::to_string(config.realtimeOffset));
      appendSetCheckbox(output, "NL", config.nodeListEnabled);
      appendSetCheckbox(output, "NB", config.nodeBroadcastEnabled);
      output << "if(typeof toggle==='function'){toggle('ESPNOW');toggle('Alexa');toggle('MQTT');toggle('Hue');toggle('Serial');}";
      break;
    case 5:
      output << "window.maxTimers=10;";
      appendSetValue(output, "NS", config.ntpServer);
      appendSetValue(output, "TZ", std::to_string(config.timezone));
      appendSetValue(output, "UO", std::to_string(config.utcOffsetSeconds));
      appendSetValue(output, "LN", config.longitude);
      appendSetValue(output, "LT", config.latitude);
      output << "if(typeof updLatLon==='function')updLatLon();";
      appendSetClassText(output, "times", 0, "Time sync disabled");
      appendSetClassText(output, "times", 1, "");
      break;
    case 6:
      appendSetValue(output, "PIN", config.settingsPin);
      appendSetValue(output, "OP", "");
      appendSetCheckbox(output, "NO", config.otaLock);
      appendSetCheckbox(output, "OW", config.wifiLock);
      appendSetCheckbox(output, "AO", config.arduinoOta);
      appendSetCheckbox(output, "SU", config.otaSameSubnet);
      appendSetClassText(output, "sip", 0, options.productName + " " + options.version);
      break;
    case 7:
      appendSetValue(output, "PU", std::to_string(config.dmxProxyUniverse));
      appendSetValue(output, "CN", std::to_string(config.dmxChannels));
      appendSetValue(output, "CS", std::to_string(config.dmxStart));
      appendSetValue(output, "CG", std::to_string(config.dmxGap));
      appendSetValue(output, "SL", std::to_string(config.dmxStartLed));
      for (size_t index = 0; index < config.dmxFixtureMap.size(); ++index) {
        appendSetValue(output, "CH" + std::to_string(index + 1), std::to_string(config.dmxFixtureMap[index]));
      }
      break;
    case 8:
      output << "d.max_gpio=64;d.um_p=[-1];d.rsvd=[];d.ro_gpio=[];d.extra=[];";
      break;
    case 10:
      appendSetValue(output, "SOMP", config.matrixEnabled ? "1" : "0");
      output << "maxPanels=64;resetPanels();";
      if (config.matrixEnabled && !config.matrixPanels.empty()) {
        appendSetValue(output, "PW", std::to_string(config.matrixPanels.front().width));
        appendSetValue(output, "PH", std::to_string(config.matrixPanels.front().height));
        appendSetValue(output, "MPC", std::to_string(config.matrixPanels.size()));
        for (size_t index = 0; index < config.matrixPanels.size(); ++index) {
          const HostPersistedConfig::MatrixPanelConfig& panel = config.matrixPanels[index];
          output << "addPanel(" << index << ");";
          appendSetValue(output, "P" + std::to_string(index) + "B", std::to_string(panel.bottomStart));
          appendSetValue(output, "P" + std::to_string(index) + "R", std::to_string(panel.rightStart));
          appendSetValue(output, "P" + std::to_string(index) + "V", std::to_string(panel.vertical));
          appendSetCheckbox(output, "P" + std::to_string(index) + "S", panel.serpentine);
          appendSetValue(output, "P" + std::to_string(index) + "X", std::to_string(panel.xOffset));
          appendSetValue(output, "P" + std::to_string(index) + "Y", std::to_string(panel.yOffset));
          appendSetValue(output, "P" + std::to_string(index) + "W", std::to_string(panel.width));
          appendSetValue(output, "P" + std::to_string(index) + "H", std::to_string(panel.height));
        }
      } else {
        appendSetValue(output, "PW", "8");
        appendSetValue(output, "PH", "8");
        appendSetValue(output, "MPC", "1");
        output << "addPanel(0);";
      }
      break;
    case 11:
      output << "d.um_p=[-1];d.rsvd=[];d.ro_gpio=[];d.touch=[];d.adc=[];d.max_gpio=64;";
      break;
    default:
      break;
  }

  output << '}';
  return output.str();
}

bool extractMultipartUpload(const HttpRequest& request, std::string& logicalPath, std::string& fileContent) {
  const auto contentType = request.headers.find("content-type");
  if (contentType == request.headers.end()) return false;

  const std::string boundaryKey = "boundary=";
  const size_t boundaryOffset = contentType->second.find(boundaryKey);
  if (boundaryOffset == std::string::npos) return false;

  const std::string boundary = "--" + contentType->second.substr(boundaryOffset + boundaryKey.size());
  const size_t headersEnd = request.body.find("\r\n\r\n");
  if (headersEnd == std::string::npos) return false;

  const std::string partHeaders = request.body.substr(0, headersEnd);
  const std::string filenameKey = "filename=\"";
  const size_t filenameOffset = partHeaders.find(filenameKey);
  if (filenameOffset == std::string::npos) return false;
  const size_t filenameEnd = partHeaders.find('"', filenameOffset + filenameKey.size());
  if (filenameEnd == std::string::npos) return false;

  logicalPath = partHeaders.substr(filenameOffset + filenameKey.size(), filenameEnd - (filenameOffset + filenameKey.size()));
  const size_t dataStart = headersEnd + 4;
  const size_t boundaryMarker = request.body.rfind("\r\n" + boundary);
  if (boundaryMarker == std::string::npos || boundaryMarker < dataStart) return false;
  fileContent = request.body.substr(dataStart, boundaryMarker - dataStart);
  return true;
}
// AI: end

std::array<uint32_t, 5> sha1(const std::string& input) {
  std::vector<uint8_t> bytes(input.begin(), input.end());
  const uint64_t bitLength = static_cast<uint64_t>(bytes.size()) * 8ULL;
  bytes.push_back(0x80);
  while ((bytes.size() % 64) != 56) bytes.push_back(0x00);
  for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<uint8_t>((bitLength >> shift) & 0xFF));

  uint32_t h0 = 0x67452301;
  uint32_t h1 = 0xEFCDAB89;
  uint32_t h2 = 0x98BADCFE;
  uint32_t h3 = 0x10325476;
  uint32_t h4 = 0xC3D2E1F0;

  for (size_t offset = 0; offset < bytes.size(); offset += 64) {
    uint32_t words[80] = {0};
    for (int i = 0; i < 16; ++i) {
      const size_t index = offset + static_cast<size_t>(i) * 4U;
      words[i] = (static_cast<uint32_t>(bytes[index]) << 24) |
                 (static_cast<uint32_t>(bytes[index + 1]) << 16) |
                 (static_cast<uint32_t>(bytes[index + 2]) << 8) |
                 static_cast<uint32_t>(bytes[index + 3]);
    }
    for (int i = 16; i < 80; ++i) {
      const uint32_t value = words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16];
      words[i] = (value << 1) | (value >> 31);
    }

    uint32_t a = h0;
    uint32_t b = h1;
    uint32_t c = h2;
    uint32_t d = h3;
    uint32_t e = h4;

    for (int i = 0; i < 80; ++i) {
      uint32_t f = 0;
      uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      const uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + words[i];
      e = d;
      d = c;
      c = (b << 30) | (b >> 2);
      b = a;
      a = temp;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  return {h0, h1, h2, h3, h4};
}

std::string base64Encode(const std::string& input) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve(((input.size() + 2) / 3) * 4);
  size_t index = 0;
  while (index < input.size()) {
    const uint32_t octetA = index < input.size() ? static_cast<uint8_t>(input[index++]) : 0;
    const uint32_t octetB = index < input.size() ? static_cast<uint8_t>(input[index++]) : 0;
    const uint32_t octetC = index < input.size() ? static_cast<uint8_t>(input[index++]) : 0;
    const uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

    output.push_back(kAlphabet[(triple >> 18) & 0x3F]);
    output.push_back(kAlphabet[(triple >> 12) & 0x3F]);
    output.push_back(index - 1 > input.size() ? '=' : kAlphabet[(triple >> 6) & 0x3F]);
    output.push_back(index > input.size() ? '=' : kAlphabet[triple & 0x3F]);
  }

  const size_t remainder = input.size() % 3;
  if (remainder > 0) {
    output[output.size() - 1] = '=';
    if (remainder == 1) output[output.size() - 2] = '=';
  }
  return output;
}

std::string websocketAcceptKey(const std::string& clientKey) {
  std::string material = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  const std::array<uint32_t, 5> digest = sha1(material);
  std::string bytes;
  bytes.reserve(20);
  for (uint32_t word : digest) {
    bytes.push_back(static_cast<char>((word >> 24) & 0xFF));
    bytes.push_back(static_cast<char>((word >> 16) & 0xFF));
    bytes.push_back(static_cast<char>((word >> 8) & 0xFF));
    bytes.push_back(static_cast<char>(word & 0xFF));
  }
  return base64Encode(bytes);
}

bool readWsFrame(int fd, std::string& payload, uint8_t& opcode) {
  uint8_t header[2] = {0, 0};
  if (recv(fd, header, sizeof(header), MSG_WAITALL) != static_cast<ssize_t>(sizeof(header))) return false;

  opcode = header[0] & 0x0F;
  const bool masked = (header[1] & 0x80U) != 0U;
  uint64_t payloadLength = header[1] & 0x7FU;
  if (payloadLength == 126U) {
    uint8_t extended[2] = {0, 0};
    if (recv(fd, extended, sizeof(extended), MSG_WAITALL) != static_cast<ssize_t>(sizeof(extended))) return false;
    payloadLength = (static_cast<uint64_t>(extended[0]) << 8) | static_cast<uint64_t>(extended[1]);
  } else if (payloadLength == 127U) {
    return false;
  }

  uint8_t mask[4] = {0, 0, 0, 0};
  if (masked && recv(fd, mask, sizeof(mask), MSG_WAITALL) != static_cast<ssize_t>(sizeof(mask))) return false;

  payload.assign(static_cast<size_t>(payloadLength), '\0');
  if (payloadLength > 0 && recv(fd, payload.data(), static_cast<size_t>(payloadLength), MSG_WAITALL) != static_cast<ssize_t>(payloadLength)) return false;
  if (masked) {
    for (size_t index = 0; index < payload.size(); ++index) payload[index] ^= static_cast<char>(mask[index % 4]);
  }
  return true;
}

bool sendWsFrame(int fd, uint8_t opcode, const std::string& payload) {
  std::vector<uint8_t> frame;
  frame.push_back(static_cast<uint8_t>(0x80U | (opcode & 0x0FU)));
  if (payload.size() < 126U) {
    frame.push_back(static_cast<uint8_t>(payload.size()));
  } else if (payload.size() <= 0xFFFFU) {
    frame.push_back(126U);
    frame.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFFU));
    frame.push_back(static_cast<uint8_t>(payload.size() & 0xFFU));
  } else {
    return false;
  }
  frame.insert(frame.end(), payload.begin(), payload.end());
  return sendAll(fd, frame.data(), frame.size());
}

} // namespace

struct HostServer::Impl : HostUsermodContext {
  HostServerOptions options;
  HostRuntimeState state;
  std::vector<uint32_t> renderBuffer;
  uint64_t renderFrameAtMs = 0;
  HostPersistedConfig persistedConfig;
  HostUsermodManager usermods;
  int listenFd = -1;
  int actualPort = -1;
  std::atomic<bool> stopRequested{false};
  std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
  mutable std::mutex stateMutex;
  std::mutex configMutex;
  std::mutex clientsMutex;
  std::mutex liveViewMutex;
  std::vector<std::shared_ptr<WsClient>> wsClients;
  std::weak_ptr<WsClient> liveViewClient;
  std::vector<std::thread> workers;
  std::vector<HostEffectCatalogEntry> effectCatalog;
  std::string effectsJson = "[\"Solid\"]";
  std::string fxDataJson = "[\"\"]";
  std::string palettesJson = "[\"Default\"]";

  // AI: below section was generated by an AI
  static std::vector<std::array<uint8_t, 4>> makeGrayPlaceholderPalette() {
    std::vector<std::array<uint8_t, 4>> stops;
    stops.reserve(16);
    for (uint8_t index = 0; index < 16; ++index) {
      stops.push_back({static_cast<uint8_t>(index << 4), 128, 128, 128});
    }
    return stops;
  }

  static bool parseCustomPaletteArray(JsonArrayConst paletteArray, std::vector<std::array<uint8_t, 4>>& stops) {
    stops.clear();
    if (paletteArray.isNull() || paletteArray.size() <= 3) return false;

    if (paletteArray[0].is<int>() && paletteArray.size() > 1 && paletteArray[1].is<const char*>()) {
      const size_t paletteSize = std::min<size_t>(paletteArray.size() - (paletteArray.size() % 2), 36);
      for (size_t index = 0; index + 1 < paletteSize; index += 2) {
        const int stopIndex = paletteArray[index].as<int>();
        if (stopIndex < 0 || stopIndex > 255) break;
        uint8_t rgbw[] = {0, 0, 0, 0};
        const char* hexColor = paletteArray[index + 1].as<const char*>();
        if (!hexColor || !colorFromHexString(rgbw, hexColor)) continue;
        stops.push_back({static_cast<uint8_t>(stopIndex), rgbw[0], rgbw[1], rgbw[2]});
      }
      return !stops.empty();
    }

    const size_t paletteSize = std::min<size_t>(paletteArray.size() - (paletteArray.size() % 4), 72);
    for (size_t index = 0; index + 3 < paletteSize; index += 4) {
      const int stopIndex = paletteArray[index].as<int>();
      if (stopIndex < 0 || stopIndex > 255) break;
      stops.push_back({
        static_cast<uint8_t>(stopIndex),
        static_cast<uint8_t>(paletteArray[index + 1].as<int>()),
        static_cast<uint8_t>(paletteArray[index + 2].as<int>()),
        static_cast<uint8_t>(paletteArray[index + 3].as<int>())
      });
    }
    return !stops.empty();
  }

  std::vector<HostCustomPaletteEntry> loadCustomPaletteEntries() {
    std::vector<HostCustomPaletteEntry> entries;
    unsigned emptyPaletteGap = 0;

    for (size_t slot = 0; slot < kHostMaxCustomPalettes; ++slot) {
      const std::string logicalPath = "/palette" + std::to_string(slot) + ".json";
      std::string rawPalette;
      std::string error;
      if (!readHostStorageFile(options.storage, logicalPath, rawPalette, error)) {
        ++emptyPaletteGap;
        if (emptyPaletteGap > kHostMaxCustomPaletteGap) break;
        continue;
      }

      for (unsigned gapIndex = 0; gapIndex < emptyPaletteGap; ++gapIndex) {
        HostCustomPaletteEntry placeholder;
        placeholder.placeholder = true;
        placeholder.stops = makeGrayPlaceholderPalette();
        entries.push_back(std::move(placeholder));
      }
      emptyPaletteGap = 0;

      DynamicJsonDocument doc(2048);
      if (deserializeJson(doc, rawPalette)) continue;
      JsonArrayConst paletteArray = doc["palette"].as<JsonArrayConst>();
      HostCustomPaletteEntry entry;
      if (!parseCustomPaletteArray(paletteArray, entry.stops)) continue;
      entries.push_back(std::move(entry));
    }

    return entries;
  }

  static void appendPaletteStops(JsonArray paletteJson, const std::vector<std::array<uint8_t, 4>>& stops) {
    for (const std::array<uint8_t, 4>& stop : stops) {
      JsonArray colors = paletteJson.createNestedArray();
      colors.add(stop[0]);
      colors.add(stop[1]);
      colors.add(stop[2]);
      colors.add(stop[3]);
    }
  }

  static uint32_t colorFromSlot(const std::array<uint8_t, 4>& slot) {
    return RGBW32(slot[0], slot[1], slot[2], slot[3]);
  }

  static uint8_t scaleChannel(uint8_t value, uint8_t scale) {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * (static_cast<uint16_t>(scale) + 1U)) >> 8);
  }

  static uint32_t scaleColor(uint32_t color, uint8_t scale) {
    return RGBW32(
      scaleChannel(static_cast<uint8_t>(color >> 16), scale),
      scaleChannel(static_cast<uint8_t>(color >> 8), scale),
      scaleChannel(static_cast<uint8_t>(color), scale),
      scaleChannel(static_cast<uint8_t>(color >> 24), scale)
    );
  }

  static uint32_t mixColor(uint32_t left, uint32_t right, uint8_t amount) {
    const uint8_t inverse = static_cast<uint8_t>(255U - amount);
    const auto mixChannel = [&](uint8_t l, uint8_t r) {
      return static_cast<uint8_t>((static_cast<uint16_t>(l) * inverse + static_cast<uint16_t>(r) * amount) / 255U);
    };
    return RGBW32(
      mixChannel(static_cast<uint8_t>(left >> 16), static_cast<uint8_t>(right >> 16)),
      mixChannel(static_cast<uint8_t>(left >> 8), static_cast<uint8_t>(right >> 8)),
      mixChannel(static_cast<uint8_t>(left), static_cast<uint8_t>(right)),
      mixChannel(static_cast<uint8_t>(left >> 24), static_cast<uint8_t>(right >> 24))
    );
  }

  static uint32_t rgbToColor(uint8_t red, uint8_t green, uint8_t blue) {
    return RGBW32(red, green, blue, 0);
  }

  static uint8_t triangleWave(uint32_t value) {
    const uint8_t phase = static_cast<uint8_t>(value & 0xFFU);
    return phase < 128U ? static_cast<uint8_t>(phase << 1) : static_cast<uint8_t>((255U - phase) << 1);
  }

  static uint32_t hsvToColor(uint8_t hue, uint8_t sat, uint8_t val) {
    const uint8_t region = hue / 43U;
    const uint8_t remainder = static_cast<uint8_t>((hue - region * 43U) * 6U);
    const uint8_t p = static_cast<uint8_t>((static_cast<uint16_t>(val) * (255U - sat)) / 255U);
    const uint8_t q = static_cast<uint8_t>((static_cast<uint16_t>(val) * (255U - ((static_cast<uint16_t>(sat) * remainder) / 255U))) / 255U);
    const uint8_t t = static_cast<uint8_t>((static_cast<uint16_t>(val) * (255U - ((static_cast<uint16_t>(sat) * (255U - remainder)) / 255U))) / 255U);

    switch (region) {
      case 0: return rgbToColor(val, t, p);
      case 1: return rgbToColor(q, val, p);
      case 2: return rgbToColor(p, val, t);
      case 3: return rgbToColor(p, q, val);
      case 4: return rgbToColor(t, p, val);
      default: return rgbToColor(val, p, q);
    }
  }

  static uint32_t pseudoRandomColor(uint32_t seed) {
    uint32_t value = seed * 1103515245U + 12345U;
    value ^= value >> 11;
    value *= 1664525U;
    return hsvToColor(static_cast<uint8_t>(value >> 16), 220, 255);
  }

  uint32_t samplePaletteColor(const HostRuntimeState& snapshot, const std::vector<HostCustomPaletteEntry>& customPalettes, uint8_t position) const {
    const uint8_t paletteId = snapshot.palette;
    if (paletteId == 0) {
      return hsvToColor(position, 255, 255);
    }
    if (paletteId == 1) {
      return pseudoRandomColor(static_cast<uint32_t>(position) * 257U + renderFrameAtMs);
    }
    if (paletteId == 2) {
      return colorFromSlot(snapshot.colors[0]);
    }
    if (paletteId == 3) {
      return mixColor(colorFromSlot(snapshot.colors[0]), colorFromSlot(snapshot.colors[1]), position);
    }
    if (paletteId == 4) {
      if (position < 128U) {
        return mixColor(colorFromSlot(snapshot.colors[2]), colorFromSlot(snapshot.colors[1]), static_cast<uint8_t>(position * 2U));
      }
      return mixColor(colorFromSlot(snapshot.colors[1]), colorFromSlot(snapshot.colors[0]), static_cast<uint8_t>((position - 128U) * 2U));
    }
    if (paletteId == 5) {
      if (position < 85U) return mixColor(colorFromSlot(snapshot.colors[0]), colorFromSlot(snapshot.colors[1]), static_cast<uint8_t>(position * 3U));
      if (position < 170U) return mixColor(colorFromSlot(snapshot.colors[1]), colorFromSlot(snapshot.colors[2]), static_cast<uint8_t>((position - 85U) * 3U));
      return mixColor(colorFromSlot(snapshot.colors[2]), colorFromSlot(snapshot.colors[0]), static_cast<uint8_t>((position - 170U) * 3U));
    }

    if (paletteId <= kHostCustomPaletteIdBase) {
      const int customIndex = static_cast<int>(kHostCustomPaletteIdBase - paletteId);
      if (customIndex >= 0 && customIndex < static_cast<int>(customPalettes.size()) && !customPalettes[static_cast<size_t>(customIndex)].stops.empty()) {
        const std::vector<std::array<uint8_t, 4>>& stops = customPalettes[static_cast<size_t>(customIndex)].stops;
        for (size_t stopIndex = 1; stopIndex < stops.size(); ++stopIndex) {
          if (position > stops[stopIndex][0]) continue;
          const std::array<uint8_t, 4>& left = stops[stopIndex - 1];
          const std::array<uint8_t, 4>& right = stops[stopIndex];
          const uint8_t span = std::max<uint8_t>(1U, static_cast<uint8_t>(right[0] - left[0]));
          const uint8_t amount = static_cast<uint8_t>((static_cast<uint16_t>(position - left[0]) * 255U) / span);
          return mixColor(
            rgbToColor(left[1], left[2], left[3]),
            rgbToColor(right[1], right[2], right[3]),
            amount
          );
        }
        const std::array<uint8_t, 4>& stop = stops.back();
        return rgbToColor(stop[1], stop[2], stop[3]);
      }
    }

    const uint8_t seed = static_cast<uint8_t>(paletteId * 37U);
    if (position < 85U) {
      return mixColor(
        rgbToColor((seed + 32U) % 256U, (seed * 3U + 64U) % 256U, (seed * 5U + 96U) % 256U),
        rgbToColor((seed * 7U + 48U) % 256U, (seed * 11U + 80U) % 256U, (seed * 13U + 16U) % 256U),
        static_cast<uint8_t>(position * 3U)
      );
    }
    if (position < 170U) {
      return mixColor(
        rgbToColor((seed * 7U + 48U) % 256U, (seed * 11U + 80U) % 256U, (seed * 13U + 16U) % 256U),
        rgbToColor((seed * 17U + 96U) % 256U, (seed * 19U + 24U) % 256U, (seed * 23U + 64U) % 256U),
        static_cast<uint8_t>((position - 85U) * 3U)
      );
    }
    return mixColor(
      rgbToColor((seed * 17U + 96U) % 256U, (seed * 19U + 24U) % 256U, (seed * 23U + 64U) % 256U),
      rgbToColor((seed * 29U + 12U) % 256U, (seed * 31U + 144U) % 256U, (seed * 41U + 200U) % 256U),
      static_cast<uint8_t>((position - 170U) * 3U)
    );
  }
  // AI: end

  bool loadConfigDocument(DynamicJsonDocument& doc, std::string& error) {
    std::ifstream input(options.storage.cfgFile, std::ios::binary);
    if (!input.is_open()) {
      error = "Unable to open config file for reading: " + options.storage.cfgFile.string();
      return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string rawConfig = buffer.str();
    if (rawConfig.empty()) {
      doc.to<JsonObject>();
      return true;
    }

    const DeserializationError parseError = deserializeJson(doc, rawConfig);
    if (parseError) {
      error = std::string("Unable to parse config file: ") + parseError.c_str();
      return false;
    }
    if (!doc.is<JsonObject>()) doc.to<JsonObject>();
    return true;
  }

  bool saveConfigDocument(const DynamicJsonDocument& doc, std::string& error) {
    std::string serialized;
    serializeJson(doc, serialized);
    return writeStorageFile("/cfg.json", serialized, error);
  }

  void loadCatalogs() {
    std::string fxSource;
    if (loadTextFile(repoSourcePath("wled00/FX.cpp"), fxSource)) {
      const std::regex dataPattern(R"(static const char (_data_FX_MODE_[A-Z0-9_]+)\[\] PROGMEM = \"((?:\\.|[^\"])*)\";)");
      const std::regex orderPattern(R"(addEffect\(FX_MODE_[A-Z0-9_]+,\s*&[^,]+,\s*(_data_FX_MODE_[A-Z0-9_]+)\);)");
      std::map<std::string, std::string> dataBySymbol;

      for (std::sregex_iterator it(fxSource.begin(), fxSource.end(), dataPattern), end; it != end; ++it) {
        dataBySymbol[(*it)[1].str()] = unescapeCppString((*it)[2].str());
      }

      effectCatalog.clear();
      effectCatalog.push_back({"Solid", "Solid"});
      for (std::sregex_iterator it(fxSource.begin(), fxSource.end(), orderPattern), end; it != end; ++it) {
        const std::string symbol = (*it)[1].str();
        if (symbol == "_data_FX_MODE_STATIC") continue;
        const auto data = dataBySymbol.find(symbol);
        if (data == dataBySymbol.end()) continue;
        effectCatalog.push_back({extractEffectName(data->second), data->second});
      }

      if (!effectCatalog.empty()) {
        DynamicJsonDocument effectsDoc(32768);
        JsonArray effects = effectsDoc.to<JsonArray>();
        for (const HostEffectCatalogEntry& entry : effectCatalog) effects.add(entry.name);
        effectsJson.clear();
        serializeJson(effectsDoc, effectsJson);

        DynamicJsonDocument fxDataDoc(65536);
        JsonArray fxData = fxDataDoc.to<JsonArray>();
        for (const HostEffectCatalogEntry& entry : effectCatalog) fxData.add(entry.data);
        fxDataJson.clear();
        serializeJson(fxDataDoc, fxDataJson);
      }
    }

    std::string paletteSource;
    if (loadTextFile(repoSourcePath("wled00/FX_fcn.cpp"), paletteSource)) {
      constexpr const char* startMarker = "const char JSON_palette_names[] PROGMEM = R\"=====(";
      constexpr const char* endMarker = ")=====\";";
      const size_t start = paletteSource.find(startMarker);
      if (start != std::string::npos) {
        const size_t contentStart = start + std::strlen(startMarker);
        const size_t end = paletteSource.find(endMarker, contentStart);
        if (end != std::string::npos) palettesJson = paletteSource.substr(contentStart, end - contentStart);
      }
    }
  }

  size_t paletteCount() const {
    DynamicJsonDocument doc(32768);
    if (deserializeJson(doc, palettesJson) || !doc.is<JsonArray>()) return 1;
    return doc.as<JsonArray>().size();
  }

  std::string registryAddress() const {
    if (actualPort > 0) {
      if (options.host == "0.0.0.0") return "127.0.0.1:" + std::to_string(actualPort);
      return options.host + ":" + std::to_string(actualPort);
    }
    if (options.port > 0) {
      if (options.host == "0.0.0.0") return "127.0.0.1:" + std::to_string(options.port);
      return options.host + ":" + std::to_string(options.port);
    }
    return options.host == "0.0.0.0" ? "127.0.0.1" : options.host;
  }

  uint64_t nowUnixMs() const {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
  }

  std::vector<HostDiscoveredNode> loadNodeRegistryEntries() const {
    std::vector<HostDiscoveredNode> entries;
    std::ifstream input(nativeNodeRegistryPath(), std::ios::binary);
    if (!input.is_open()) return entries;

    std::ostringstream buffer;
    buffer << input.rdbuf();
    DynamicJsonDocument doc(32768);
    if (deserializeJson(doc, buffer.str())) return entries;
    JsonArrayConst rawEntries = doc["entries"].as<JsonArrayConst>();
    for (JsonObjectConst rawEntry : rawEntries) {
      HostDiscoveredNode entry;
      if (rawEntry["instanceId"].is<const char*>()) entry.instanceId = rawEntry["instanceId"].as<const char*>();
      if (rawEntry["name"].is<const char*>()) entry.name = rawEntry["name"].as<const char*>();
      if (rawEntry["ip"].is<const char*>()) entry.ip = rawEntry["ip"].as<const char*>();
      if (rawEntry["port"].is<int>()) entry.port = rawEntry["port"].as<int>();
      if (rawEntry["updatedAtMs"].is<uint64_t>()) entry.updatedAtMs = rawEntry["updatedAtMs"].as<uint64_t>();
      if (rawEntry["type"].is<uint8_t>()) entry.type = rawEntry["type"].as<uint8_t>();
      if (rawEntry["vid"].is<uint32_t>()) entry.vid = rawEntry["vid"].as<uint32_t>();
      if (!entry.instanceId.empty()) entries.push_back(std::move(entry));
    }
    return entries;
  }

  bool saveNodeRegistryEntries(const std::vector<HostDiscoveredNode>& entries) const {
    DynamicJsonDocument doc(32768);
    JsonObject root = doc.to<JsonObject>();
    JsonArray items = root.createNestedArray("entries");
    for (const HostDiscoveredNode& entry : entries) {
      JsonObject object = items.createNestedObject();
      object["instanceId"] = entry.instanceId.c_str();
      object["name"] = entry.name.c_str();
      object["ip"] = entry.ip.c_str();
      object["port"] = entry.port;
      object["updatedAtMs"] = entry.updatedAtMs;
      object["type"] = entry.type;
      object["vid"] = entry.vid;
    }

    std::error_code mkdirError;
    std::filesystem::create_directories(nativeNodeRegistryPath().parent_path(), mkdirError);
    std::ofstream output(nativeNodeRegistryPath(), std::ios::binary | std::ios::trunc);
    if (!output.is_open()) return false;
    std::string serialized;
    serializeJson(doc, serialized);
    output << serialized;
    return output.good();
  }

  void refreshSelfNodeRegistryEntry() {
    std::vector<HostDiscoveredNode> entries = loadNodeRegistryEntries();
    const uint64_t now = nowUnixMs();
    const uint64_t staleBefore = now > 30000 ? now - 30000 : 0;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const HostDiscoveredNode& entry) {
      return entry.updatedAtMs < staleBefore || entry.instanceId == options.instanceId;
    }), entries.end());

    HostDiscoveredNode self;
    self.instanceId = options.instanceId;
    {
      std::lock_guard<std::mutex> configLock(configMutex);
      self.name = persistedConfig.deviceName;
    }
    self.ip = registryAddress();
    self.port = actualPort > 0 ? actualPort : options.port;
    self.updatedAtMs = now;
    self.type = 128;
    self.vid = 1700000;
    entries.push_back(std::move(self));
    saveNodeRegistryEntries(entries);
  }

  void removeSelfNodeRegistryEntry() {
    std::vector<HostDiscoveredNode> entries = loadNodeRegistryEntries();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const HostDiscoveredNode& entry) {
      return entry.instanceId == options.instanceId;
    }), entries.end());
    saveNodeRegistryEntries(entries);
  }

  std::vector<HostDiscoveredNode> visibleNodeEntries() {
    std::vector<HostDiscoveredNode> entries = loadNodeRegistryEntries();
    const uint64_t now = nowUnixMs();
    const uint64_t staleBefore = now > 30000 ? now - 30000 : 0;
    HostPersistedConfig configSnapshot;
    {
      std::lock_guard<std::mutex> configLock(configMutex);
      configSnapshot = persistedConfig;
    }
    if (!configSnapshot.nodeListEnabled) return {};

    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const HostDiscoveredNode& entry) {
      return entry.instanceId == options.instanceId || entry.updatedAtMs < staleBefore;
    }), entries.end());
    return entries;
  }

  std::string buildNodesJson() {
    const std::vector<HostDiscoveredNode> entries = visibleNodeEntries();
    DynamicJsonDocument doc(8192);
    JsonArray nodes = doc.createNestedArray("nodes");
    const uint64_t now = nowUnixMs();
    for (const HostDiscoveredNode& entry : entries) {
      JsonObject node = nodes.createNestedObject();
      node["name"] = entry.name.c_str();
      node["type"] = entry.type;
      const std::string address = entry.port > 0 && entry.ip.find(':') == std::string::npos ? (entry.ip + ":" + std::to_string(entry.port)) : entry.ip;
      node["ip"] = address.c_str();
      node["age"] = static_cast<uint8_t>(std::min<uint64_t>(255, entry.updatedAtMs > now ? 0 : (now - entry.updatedAtMs) / 1000));
      node["vid"] = entry.vid;
    }
    std::string output;
    serializeJson(doc, output);
    return output;
  }

  void syncPlaylistGlobalsLocked() {
    bri = state.bri;
    transitionDelay = state.transition;
    nightlightActive = false;
    currentPreset = state.preset;
  }

  void applyConfigToRuntimeState() {
    std::lock_guard<std::mutex> stateLock(stateMutex);
    state.ledCount = persistedConfig.ledCount;
    state.transition = persistedConfig.transition;
    state.bri = persistedConfig.brightness;
    syncPlaylistGlobalsLocked();
  }

  bool persistBootPreset(uint8_t presetId, std::string& error) {
    DynamicJsonDocument doc(8192);
    if (!loadConfigDocument(doc, error)) return false;
    HostPersistedConfig nextConfig;
    loadConfigSnapshotFromJson(doc.as<JsonObjectConst>(), nextConfig);
    nextConfig.bootPreset = presetId;
    writeConfigSnapshotToJson(nextConfig, doc.as<JsonObject>());
    JsonObject usermodRoot = doc["um"].is<JsonObject>() ? doc["um"].as<JsonObject>() : doc.createNestedObject("um");
    usermods.addToConfig(usermodRoot);
    if (!saveConfigDocument(doc, error)) return false;
    {
      std::lock_guard<std::mutex> configLock(configMutex);
      persistedConfig = nextConfig;
    }
    return true;
  }

  bool refreshPersistedConfig(std::string& error) {
    DynamicJsonDocument doc(8192);
    if (!loadConfigDocument(doc, error)) return false;

    HostPersistedConfig nextConfig;
    loadConfigSnapshotFromJson(doc.as<JsonObjectConst>(), nextConfig);
    usermods.readFromConfig(doc["um"].as<JsonObjectConst>());

    {
      std::lock_guard<std::mutex> configLock(configMutex);
      persistedConfig = nextConfig;
    }
    applyConfigToRuntimeState();
    return true;
  }

  std::string buildCfgJson() {
    std::lock_guard<std::mutex> configLock(configMutex);
    DynamicJsonDocument doc(8192);
    JsonObject root = doc.to<JsonObject>();
    writeConfigSnapshotToJson(persistedConfig, root);
    JsonObject usermodRoot = root.createNestedObject("um");
    usermods.addToConfig(usermodRoot);
    std::string output;
    serializeJson(doc, output);
    return output;
  }

  bool persistSettings(const std::string& logicalPath, const FormFields& formFields, std::string& error) {
    DynamicJsonDocument doc(8192);
    if (!loadConfigDocument(doc, error)) return false;

    HostPersistedConfig nextConfig;
    loadConfigSnapshotFromJson(doc.as<JsonObjectConst>(), nextConfig);
    JsonObject usermodRoot = doc["um"].is<JsonObject>() ? doc["um"].as<JsonObject>() : doc.createNestedObject("um");

    if (logicalPath == "/settings/ui") {
      parseStringField(formFields, "DS", nextConfig.deviceName);
      parseCheckboxField(formFields, "SU", nextConfig.simplifiedUi);
    } else if (logicalPath == "/settings/wifi") {
      parseStringField(formFields, "CM", nextConfig.mdnsName);
      parseStringField(formFields, "AS", nextConfig.apSsid);
      parseUint8Field(formFields, "AC", nextConfig.apChannel);
      parseUint8Field(formFields, "D0", nextConfig.dns[0]);
      parseUint8Field(formFields, "D1", nextConfig.dns[1]);
      parseUint8Field(formFields, "D2", nextConfig.dns[2]);
      parseUint8Field(formFields, "D3", nextConfig.dns[3]);
    } else if (logicalPath == "/settings/leds") {
      parseUint16Field(formFields, "LC0", nextConfig.ledCount);
      parseUint16Field(formFields, "TD", nextConfig.transition);
      parseUint8Field(formFields, "CA", nextConfig.brightness);
    } else if (logicalPath == "/settings/sync") {
      parseUint16Field(formFields, "UP", nextConfig.udpPort);
      parseUint16Field(formFields, "U2", nextConfig.udpPort2);
      parseUint8Field(formFields, "UR", nextConfig.udpRetries);
      parseUint16Field(formFields, "EP", nextConfig.realtimePort);
      parseUint16Field(formFields, "EU", nextConfig.realtimeUniverse);
      parseUint16Field(formFields, "DA", nextConfig.dmxAddress);
      parseUint16Field(formFields, "XX", nextConfig.dmxSpacing);
      parseUint8Field(formFields, "PY", nextConfig.e131Priority);
      parseUint8Field(formFields, "DM", nextConfig.dmxMode);
      parseUint16Field(formFields, "ET", nextConfig.realtimeTimeoutMs);
      parseInt16Field(formFields, "WO", nextConfig.realtimeOffset);
      parseCheckboxField(formFields, "NL", nextConfig.nodeListEnabled);
      parseCheckboxField(formFields, "NB", nextConfig.nodeBroadcastEnabled);
    } else if (logicalPath == "/settings/time") {
      parseStringField(formFields, "NS", nextConfig.ntpServer);
      parseUint8Field(formFields, "TZ", nextConfig.timezone);
      if (const auto field = formFields.find("UO"); field != formFields.end()) nextConfig.utcOffsetSeconds = std::strtol(field->second.c_str(), nullptr, 10);
      parseStringField(formFields, "LN", nextConfig.longitude);
      parseStringField(formFields, "LT", nextConfig.latitude);
    } else if (logicalPath == "/settings/sec") {
      parseStringField(formFields, "PIN", nextConfig.settingsPin);
      parseCheckboxField(formFields, "NO", nextConfig.otaLock);
      parseCheckboxField(formFields, "OW", nextConfig.wifiLock);
      parseCheckboxField(formFields, "AO", nextConfig.arduinoOta);
      parseCheckboxField(formFields, "SU", nextConfig.otaSameSubnet);
    } else if (logicalPath == "/settings/dmx") {
      parseUint16Field(formFields, "PU", nextConfig.dmxProxyUniverse);
      parseUint8Field(formFields, "CN", nextConfig.dmxChannels);
      parseUint16Field(formFields, "CS", nextConfig.dmxStart);
      parseUint16Field(formFields, "CG", nextConfig.dmxGap);
      parseUint16Field(formFields, "SL", nextConfig.dmxStartLed);
      for (size_t index = 0; index < nextConfig.dmxFixtureMap.size(); ++index) {
        parseUint8Field(formFields, ("CH" + std::to_string(index + 1)).c_str(), nextConfig.dmxFixtureMap[index]);
      }
    } else if (logicalPath == "/settings/2D") {
      if (const auto field = formFields.find("SOMP"); field != formFields.end()) nextConfig.matrixEnabled = parseBoolValue(field->second);
      nextConfig.matrixPanels.clear();
      uint16_t panelCount = 1;
      parseUint16Field(formFields, "MPC", panelCount);
      panelCount = std::max<uint16_t>(1, std::min<uint16_t>(panelCount, 64));
      if (nextConfig.matrixEnabled) {
        nextConfig.matrixPanels.reserve(panelCount);
        for (uint16_t index = 0; index < panelCount; ++index) {
          HostPersistedConfig::MatrixPanelConfig panel;
          parseUint8Field(formFields, ("P" + std::to_string(index) + "B").c_str(), panel.bottomStart);
          parseUint8Field(formFields, ("P" + std::to_string(index) + "R").c_str(), panel.rightStart);
          parseUint8Field(formFields, ("P" + std::to_string(index) + "V").c_str(), panel.vertical);
          parseCheckboxField(formFields, ("P" + std::to_string(index) + "S").c_str(), panel.serpentine);
          parseUint16Field(formFields, ("P" + std::to_string(index) + "X").c_str(), panel.xOffset);
          parseUint16Field(formFields, ("P" + std::to_string(index) + "Y").c_str(), panel.yOffset);
          parseUint16Field(formFields, ("P" + std::to_string(index) + "W").c_str(), panel.width);
          parseUint16Field(formFields, ("P" + std::to_string(index) + "H").c_str(), panel.height);
          nextConfig.matrixPanels.push_back(panel);
        }
      }
    } else if (logicalPath == "/settings/um") {
      DynamicJsonDocument defaultsDoc(2048);
      JsonObject defaultsRoot = defaultsDoc.to<JsonObject>();
      usermods.addToConfig(defaultsRoot);
      const JsonObjectConst defaultsRootConst = defaultsRoot;
      mergeMissingJsonObject(usermodRoot, defaultsRootConst);
      applyFormFieldsToJsonObject(usermodRoot, "", formFields);
      const JsonObjectConst usermodRootConst = usermodRoot;
      usermods.readFromConfig(usermodRootConst);
    } else {
      error = "Settings persistence is not implemented for this page yet.";
      return false;
    }

    writeConfigSnapshotToJson(nextConfig, doc.as<JsonObject>());
    if (!saveConfigDocument(doc, error)) return false;

    {
      std::lock_guard<std::mutex> configLock(configMutex);
      persistedConfig = nextConfig;
    }
    applyConfigToRuntimeState();
    return true;
  }

  std::string buildInfoJson() {
    const std::vector<HostCustomPaletteEntry> customPalettes = loadCustomPaletteEntries();
    HostPersistedConfig configSnapshot;
    {
      std::lock_guard<std::mutex> configLock(configMutex);
      configSnapshot = persistedConfig;
    }
    const std::vector<HostDiscoveredNode> visibleNodes = configSnapshot.nodeListEnabled ? visibleNodeEntries() : std::vector<HostDiscoveredNode>{};
    const uint64_t usedBytes = recursiveFileBytes(options.storage.configDir);
    const uint64_t totalBytes = filesystemCapacityBytes(options.storage.configDir);
    const uint64_t presetsModifiedTime = fileModifiedTimeSeconds(options.storage.presetsFile);
    const std::string localTime = formatLocalTimeString();
    const std::string hostAddress = registryAddress();
    DynamicJsonDocument doc(4096);
    doc["brand"] = options.productName.c_str();
    doc["product"] = options.productName.c_str();
    doc["name"] = configSnapshot.deviceName.c_str();
    doc["ver"] = options.version.c_str();
    doc["vid"] = 1700000;
    doc["cn"] = "Kuuhaku";
    doc["release"] = "WLED_Native";
    doc["repo"] = "wled-dev/WLED";
    doc["arch"] = "native";
    doc["core"] = "native";
    doc["mac"] = options.instanceId.c_str();
    doc["psram"] = 0;
    doc["psrSz"] = 0;
    doc["live"] = false;
    doc["liveseg"] = -1;
    doc["lm"] = "";
    doc["lip"] = "";
    doc["str"] = false;
    doc["simplifiedui"] = configSnapshot.simplifiedUi;
    doc["udpport"] = configSnapshot.udpPort;
    doc["fxcount"] = effectCatalog.empty() ? 1 : static_cast<int>(effectCatalog.size());
    doc["palcount"] = static_cast<int>(paletteCount());
    doc["cpalcount"] = static_cast<int>(customPalettes.size());
    doc["umpalcount"] = 0;
    doc["cpalmax"] = static_cast<int>(kHostMaxCustomPalettes);
    doc["ws"] = 1;
    doc["ndc"] = configSnapshot.nodeListEnabled ? static_cast<int>(visibleNodes.size()) : -1;
    doc["freeheap"] = 0;
    doc["uptime"] = millis() / 1000;
    doc["time"] = localTime.c_str();
    doc["clock"] = 0;
    doc["flash"] = 0;
    doc["lwip"] = 0;
    doc["opt"] = 0x08;
    doc["deviceId"] = options.instanceId.c_str();
    doc["ip"] = hostAddress.c_str();

    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["bssid"] = "";
    wifi["rssi"] = 0;
    wifi["signal"] = 100;
    wifi["channel"] = 0;
    wifi["ap"] = false;

    JsonObject fs = doc.createNestedObject("fs");
    fs["u"] = static_cast<uint32_t>(usedBytes / 1000);
    fs["t"] = static_cast<uint32_t>(std::max<uint64_t>(usedBytes, totalBytes) / 1000);
    fs["pmt"] = presetsModifiedTime;

    JsonObject leds = doc.createNestedObject("leds");
    {
      std::lock_guard<std::mutex> stateLock(stateMutex);
      leds["count"] = state.ledCount;
    }
    leds["rgbw"] = false;
    leds["wv"] = 0;
    leds["fps"] = 0;
    leds["pwr"] = 0;
    leds["maxpwr"] = 0;
    leds["maxseg"] = 1;
    leds["lc"] = 1;
    {
      leds["bootps"] = configSnapshot.bootPreset;
    }
    JsonArray seglc = leds.createNestedArray("seglc");
    seglc.add(1);
    doc.createNestedArray("umpalnames");
    JsonObject root = doc.as<JsonObject>();
    usermods.addToJsonInfo(*this, root);

    std::string output;
    serializeJson(doc, output);
    return output;
  }

  std::string buildStateJson() {
    DynamicJsonDocument doc(4096);
    std::lock_guard<std::mutex> lock(stateMutex);

    doc["on"] = state.on;
    doc["bri"] = state.bri;
    doc["transition"] = state.transition;
    doc["bs"] = 0;
    doc["ps"] = state.preset;
    doc["pl"] = state.playlistActive ? state.playlist : -1;
    doc["lor"] = 0;
    doc["mainseg"] = 0;
    doc["nl"]["on"] = false;
    doc["nl"]["dur"] = 0;
    doc["nl"]["mode"] = 0;
    doc["nl"]["tbri"] = 0;
    doc["nl"]["rem"] = 0;
    doc["udpn"]["send"] = false;
    doc["udpn"]["recv"] = false;
    doc["udpn"]["sgrp"] = 0;
    doc["udpn"]["rgrp"] = 0;

    JsonArray segments = doc.createNestedArray("seg");
    JsonObject segment = segments.createNestedObject();
    segment["id"] = 0;
    segment["start"] = 0;
    segment["stop"] = state.ledCount;
    segment["startY"] = 0;
    segment["stopY"] = 0;
    segment["len"] = state.ledCount;
    segment["grp"] = 1;
    segment["spc"] = 0;
    segment["of"] = 0;
    segment["on"] = state.on;
    segment["bri"] = state.segmentBri;
    segment["fx"] = state.effect;
    segment["sx"] = state.speed;
    segment["ix"] = state.intensity;
    segment["pal"] = state.palette;
    segment["sel"] = true;
    segment["rev"] = false;
    segment["mi"] = false;
    segment["frz"] = false;
    segment["set"] = 0;
    segment["m12"] = 0;
    segment["si"] = 0;
    segment["bm"] = 0;
    segment["lc"] = 1;
    segment["cct"] = 0;
    segment["c1"] = 0;
    segment["c2"] = 0;
    segment["c3"] = 0;
    segment["o1"] = false;
    segment["o2"] = false;
    segment["o3"] = false;
    JsonArray colors = segment.createNestedArray("col");
    for (const std::array<uint8_t, 4>& slot : state.colors) {
      JsonArray color = colors.createNestedArray();
      color.add(slot[0]);
      color.add(slot[1]);
      color.add(slot[2]);
      color.add(slot[3]);
    }
    JsonObject root = doc.as<JsonObject>();
    usermods.addToJsonState(*this, root);

    std::string output;
    serializeJson(doc, output);
    return output;
  }

  DynamicJsonDocument captureCurrentPresetDocument(const char* name) {
    DynamicJsonDocument document(4096);
    JsonObject root = document.to<JsonObject>();
    root["n"] = name ? name : "Autosave";
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      root["on"] = state.on;
      root["bri"] = state.bri;
      root["transition"] = state.transition;
      JsonArray segments = root.createNestedArray("seg");
      JsonObject segment = segments.createNestedObject();
      segment["on"] = state.on;
      segment["bri"] = state.segmentBri;
      segment["fx"] = state.effect;
      segment["sx"] = state.speed;
      segment["ix"] = state.intensity;
      segment["pal"] = state.palette;
      JsonArray colors = segment.createNestedArray("col");
      for (const std::array<uint8_t, 4>& slot : state.colors) {
        JsonArray color = colors.createNestedArray();
        for (const uint8_t channel : slot) color.add(channel);
      }
    }
    return document;
  }

  bool writePresetDocument(uint8_t presetId, DynamicJsonDocument& document) {
    return writeObjectToFileUsingId(getPresetsFileName(), presetId, &document);
  }

  void unloadPlaylistLocked() {
    unloadPlaylist();
    state.playlistActive = false;
    state.playlist = -1;
  }

  uint64_t millis() const override {
    const auto elapsed = std::chrono::steady_clock::now() - startTime;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
  }

  HostRuntimeState getState() const override {
    std::lock_guard<std::mutex> lock(stateMutex);
    return state;
  }

  std::string buildCombinedJson() {
    DynamicJsonDocument doc(4096);
    DynamicJsonDocument stateDoc(2048);
    DynamicJsonDocument infoDoc(2048);
    deserializeJson(stateDoc, buildStateJson());
    deserializeJson(infoDoc, buildInfoJson());
    doc["state"] = stateDoc.as<JsonVariant>();
    doc["info"] = infoDoc.as<JsonVariant>();
    std::string output;
    serializeJson(doc, output);
    return output;
  }

  std::string buildAllJson() {
    DynamicJsonDocument doc(65536);
    DynamicJsonDocument stateDoc(4096);
    DynamicJsonDocument infoDoc(4096);
    DynamicJsonDocument effectsDoc(32768);
    DynamicJsonDocument palettesDoc(32768);
    deserializeJson(stateDoc, buildStateJson());
    deserializeJson(infoDoc, buildInfoJson());
    deserializeJson(effectsDoc, effectsJson);
    deserializeJson(palettesDoc, palettesJson);
    doc["state"] = stateDoc.as<JsonVariant>();
    doc["info"] = infoDoc.as<JsonVariant>();
    doc["effects"] = effectsDoc.as<JsonVariant>();
    doc["palettes"] = palettesDoc.as<JsonVariant>();
    std::string output;
    serializeJson(doc, output);
    return output;
  }

  std::string buildPalettePageJson(int page) {
    const std::vector<HostCustomPaletteEntry> customPalettes = loadCustomPaletteEntries();
    DynamicJsonDocument namesDoc(32768);
    if (deserializeJson(namesDoc, palettesJson) || !namesDoc.is<JsonArray>()) return "{\"m\":0,\"p\":{}}";

    JsonArray names = namesDoc.as<JsonArray>();
    constexpr int itemPerPage = 8;
    const int palettesCount = static_cast<int>(names.size());
    const int totalPaletteCount = palettesCount + static_cast<int>(customPalettes.size());
    const int maxPage = totalPaletteCount > 0 ? (totalPaletteCount - 1) / itemPerPage : 0;
    if (page < 0) page = 0;
    if (page > maxPage) page = maxPage;

    DynamicJsonDocument doc(16384);
    doc["m"] = maxPage;
    JsonObject previews = doc.createNestedObject("p");

    const int start = page * itemPerPage;
    const int end = std::min(start + itemPerPage, totalPaletteCount);
    for (int index = start; index < end; ++index) {
      const int paletteId = index >= palettesCount ? (kHostCustomPaletteIdBase - (index - palettesCount)) : index;
      JsonArray palette = previews.createNestedArray(std::to_string(paletteId));
      switch (index) {
        case 0: {
          JsonArray a = palette.createNestedArray(); a.add(0); a.add(0); a.add(255); a.add(0);
          JsonArray b = palette.createNestedArray(); b.add(127); b.add(255); b.add(0); b.add(0);
          JsonArray c = palette.createNestedArray(); c.add(255); c.add(0); c.add(0); c.add(255);
          break;
        }
        case 1:
          palette.add("r");
          palette.add("r");
          palette.add("r");
          palette.add("r");
          break;
        case 2:
          palette.add("c1");
          break;
        case 3:
          palette.add("c1");
          palette.add("c1");
          palette.add("c2");
          palette.add("c2");
          break;
        case 4:
          palette.add("c3");
          palette.add("c2");
          palette.add("c1");
          break;
        case 5:
          for (int i = 0; i < 5; ++i) palette.add("c1");
          for (int i = 0; i < 5; ++i) palette.add("c2");
          for (int i = 0; i < 5; ++i) palette.add("c3");
          palette.add("c1");
          break;
        default: {
          if (index >= palettesCount) {
            appendPaletteStops(palette, customPalettes[static_cast<size_t>(index - palettesCount)].stops);
            break;
          }
          const uint8_t seed = static_cast<uint8_t>(index * 37);
          JsonArray a = palette.createNestedArray(); a.add(0); a.add((seed + 32) % 256); a.add((seed * 3 + 64) % 256); a.add((seed * 5 + 96) % 256);
          JsonArray b = palette.createNestedArray(); b.add(85); b.add((seed * 7 + 48) % 256); b.add((seed * 11 + 80) % 256); b.add((seed * 13 + 16) % 256);
          JsonArray c = palette.createNestedArray(); c.add(170); c.add((seed * 17 + 96) % 256); c.add((seed * 19 + 24) % 256); c.add((seed * 23 + 64) % 256);
          JsonArray d = palette.createNestedArray(); d.add(255); d.add((seed * 29 + 12) % 256); d.add((seed * 31 + 144) % 256); d.add((seed * 41 + 200) % 256);
          break;
        }
      }
    }

    std::string output;
    serializeJson(doc, output);
    return output;
  }

  bool applyPreset(uint8_t presetId) override {
    return applyPresetSelection(presetId);
  }

  bool savePreset(uint8_t presetId, const char* name) override {
    DynamicJsonDocument document = captureCurrentPresetDocument(name);
    return writePresetDocument(presetId, document);
  }

  bool applyPresetSelection(uint8_t presetId) {
    if (presetId == 0) return false;

    DynamicJsonDocument document(4096);
    if (!readObjectFromFileUsingId(getPresetsFileName(), presetId, &document)) return false;

    JsonObjectConst preset = document.as<JsonObjectConst>();
    if (preset.isNull()) return false;

    if (preset["playlist"].is<JsonObjectConst>()) {
      {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.playlist = presetId;
        state.playlistActive = true;
        state.on = true;
        syncPlaylistGlobalsLocked();
      }
      DynamicJsonDocument playlistDoc(2048);
      playlistDoc.set(preset["playlist"]);
      JsonObject playlist = playlistDoc.as<JsonObject>();
      if (loadPlaylist(playlist, presetId) < 0) return false;
      doAdvancePlaylist = true;
      setHostMillis(static_cast<uint32_t>(millis()));
      handlePlaylist();
      return true;
    }

    std::lock_guard<std::mutex> lock(stateMutex);
    unloadPlaylistLocked();
    state.preset = presetId;
    if (preset["on"].is<bool>()) state.on = preset["on"].as<bool>();
    if (preset["bri"].is<uint8_t>()) state.bri = preset["bri"].as<uint8_t>();
    if (preset["transition"].is<uint16_t>()) state.transition = preset["transition"].as<uint16_t>();

    if (preset["seg"].is<JsonArrayConst>()) {
      JsonArrayConst segments = preset["seg"].as<JsonArrayConst>();
      for (JsonVariantConst segmentValue : segments) {
        if (!segmentValue.is<JsonObjectConst>()) continue;
        JsonObjectConst segment = segmentValue.as<JsonObjectConst>();
        if (segment["on"].is<bool>()) state.on = segment["on"].as<bool>();
        if (segment["bri"].is<uint8_t>()) state.segmentBri = segment["bri"].as<uint8_t>();
        if (segment["fx"].is<uint8_t>()) state.effect = segment["fx"].as<uint8_t>();
        if (segment["sx"].is<uint8_t>()) state.speed = segment["sx"].as<uint8_t>();
        if (segment["ix"].is<uint8_t>()) state.intensity = segment["ix"].as<uint8_t>();
        if (segment["pal"].is<uint8_t>()) state.palette = segment["pal"].as<uint8_t>();
        if (segment["col"].is<JsonArrayConst>()) {
          JsonArrayConst colors = segment["col"].as<JsonArrayConst>();
          for (size_t slotIndex = 0; slotIndex < state.colors.size() && slotIndex < colors.size(); ++slotIndex) {
            if (!colors[slotIndex].is<JsonArrayConst>()) continue;
            JsonArrayConst color = colors[slotIndex].as<JsonArrayConst>();
            for (size_t channelIndex = 0; channelIndex < state.colors[slotIndex].size(); ++channelIndex) {
              state.colors[slotIndex][channelIndex] = color[channelIndex] | 0;
            }
          }
        }
        break;
      }
    }

    syncPlaylistGlobalsLocked();

    return true;
  }

  void applySegmentPatchLocked(JsonObjectConst segment) {
    if (segment["on"].is<bool>()) state.on = segment["on"].as<bool>();
    if (segment["bri"].is<uint8_t>()) state.segmentBri = segment["bri"].as<uint8_t>();
    if (segment["fx"].is<uint8_t>()) state.effect = segment["fx"].as<uint8_t>();
    if (segment["sx"].is<uint8_t>()) state.speed = segment["sx"].as<uint8_t>();
    if (segment["ix"].is<uint8_t>()) state.intensity = segment["ix"].as<uint8_t>();
    if (segment["pal"].is<uint8_t>()) state.palette = segment["pal"].as<uint8_t>();
    if (segment["col"].is<JsonArrayConst>()) {
      JsonArrayConst colors = segment["col"].as<JsonArrayConst>();
      for (size_t slotIndex = 0; slotIndex < state.colors.size() && slotIndex < colors.size(); ++slotIndex) {
        if (!colors[slotIndex].is<JsonArrayConst>()) continue;
        JsonArrayConst color = colors[slotIndex].as<JsonArrayConst>();
        for (size_t channelIndex = 0; channelIndex < state.colors[slotIndex].size(); ++channelIndex) {
          state.colors[slotIndex][channelIndex] = color[channelIndex] | 0;
        }
      }
    }
  }

  bool savePresetFromJson(JsonObjectConst root, std::string& error) {
    if (!root["psave"].is<uint8_t>()) return true;
    const uint8_t presetId = root["psave"].as<uint8_t>();
    if (presetId == 0 || presetId > 250) return false;

    if (root["bootps"].is<uint8_t>() && !persistBootPreset(root["bootps"].as<uint8_t>(), error)) return false;

    const char* name = root["n"].is<const char*>() ? root["n"].as<const char*>() : nullptr;
    if (root["playlist"].is<JsonObjectConst>()) {
      DynamicJsonDocument document(4096);
      document.set(root);
      JsonObject object = document.as<JsonObject>();
      object.remove("psave");
      object.remove("pd");
      object.remove("ps");
      object.remove("bootps");
      object.remove("v");
      object.remove("time");
      object.remove("error");
      object.remove("o");
      if (!object["n"].is<const char*>()) object["n"] = name ? name : "Preset";
      return writePresetDocument(presetId, document);
    }

    if (root["o"].is<bool>() && root["o"].as<bool>()) {
      DynamicJsonDocument document(4096);
      document.set(root);
      JsonObject object = document.as<JsonObject>();
      object.remove("psave");
      object.remove("pd");
      object.remove("ps");
      object.remove("bootps");
      object.remove("v");
      object.remove("time");
      object.remove("error");
      object.remove("o");
      if (!object["n"].is<const char*>()) object["n"] = name ? name : "Preset";
      return writePresetDocument(presetId, document);
    }

    DynamicJsonDocument document = captureCurrentPresetDocument(name ? name : "Preset");
    return writePresetDocument(presetId, document);
  }

  bool removeCustomPalette(uint8_t slot, std::string& error) {
    const std::string logicalPath = "/palette" + std::to_string(slot) + ".json";
    std::string deleteError;
    if (!deleteHostStorageFile(options.storage, logicalPath, deleteError)) {
      if (deleteError.find("file does not exist") != std::string::npos) {
        error.clear();
        return true;
      }
      error = deleteError;
      return false;
    }
    error.clear();
    return true;
  }

  bool loadPlaylistFromJson(JsonObjectConst playlistRoot, int16_t presetId, bool forceOn) {
    DynamicJsonDocument playlistDoc(2048);
    playlistDoc.set(playlistRoot);
    JsonObject playlist = playlistDoc.as<JsonObject>();

    {
      std::lock_guard<std::mutex> lock(stateMutex);
      state.playlist = presetId;
      state.playlistActive = !playlistRoot.isNull() && playlistRoot.size() > 0;
      if (forceOn) state.on = true;
      syncPlaylistGlobalsLocked();
    }

    if (playlistRoot.isNull() || playlistRoot.size() == 0) {
      std::lock_guard<std::mutex> lock(stateMutex);
      unloadPlaylistLocked();
      return true;
    }

    if (loadPlaylist(playlist, presetId > 0 ? static_cast<byte>(presetId) : 0) < 0) return false;
    doAdvancePlaylist = true;
    setHostMillis(static_cast<uint32_t>(millis()));
    handlePlaylist();
    return true;
  }

  void applyStatePatch(JsonVariantConst patch) {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (patch["on"].is<bool>()) state.on = patch["on"].as<bool>();
    if (patch["bri"].is<uint8_t>()) state.bri = patch["bri"].as<uint8_t>();
    if (patch["transition"].is<uint16_t>()) state.transition = patch["transition"].as<uint16_t>();
    if (patch["pd"].is<uint8_t>()) {
      unloadPlaylistLocked();
      state.preset = patch["pd"].as<uint8_t>();
    }
    if (patch["ps"].is<uint8_t>()) state.preset = patch["ps"].as<uint8_t>();
    if (patch["pl"].is<int>()) state.playlist = patch["pl"].as<int16_t>();
    if (patch.containsKey("playlist")) state.playlistActive = patch["playlist"].is<JsonObjectConst>() && patch["playlist"].as<JsonObjectConst>().size() > 0;

    if (patch["seg"].is<JsonObjectConst>()) {
      applySegmentPatchLocked(patch["seg"].as<JsonObjectConst>());
    } else if (patch["seg"].is<JsonArrayConst>()) {
      JsonArrayConst segments = patch["seg"].as<JsonArrayConst>();
      for (JsonVariantConst segmentValue : segments) {
        if (!segmentValue.is<JsonObjectConst>()) continue;
        applySegmentPatchLocked(segmentValue.as<JsonObjectConst>());
      }
    }
    syncPlaylistGlobalsLocked();
  }

  void broadcastState() {
    const std::string payload = buildCombinedJson();
    std::lock_guard<std::mutex> lock(clientsMutex);
    auto it = wsClients.begin();
    while (it != wsClients.end()) {
      const std::shared_ptr<WsClient>& client = *it;
      bool sendOk = false;
      if (client && client->fd >= 0) {
        std::lock_guard<std::mutex> clientLock(client->writeMutex);
        sendOk = sendWsFrame(client->fd, 0x1, payload);
      }
      if (!sendOk) {
        if (client && client->fd >= 0) close(client->fd);
        it = wsClients.erase(it);
        continue;
      }
      ++it;
    }
  }

  void broadcastStateAsync() {
    std::thread([this]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
      if (!stopRequested.load()) broadcastState();
    }).detach();
  }

  void renderFrameLocked(uint64_t nowMs) {
    renderFrameAtMs = nowMs;
    const uint16_t ledCount = std::max<uint16_t>(1, std::min<uint16_t>(state.ledCount, 1024));
    if (renderBuffer.size() != ledCount) renderBuffer.assign(ledCount, 0);

    const uint8_t brightness = state.on ? static_cast<uint8_t>((static_cast<uint16_t>(state.bri) * state.segmentBri) / 255U) : 0;
    if (brightness == 0) {
      std::fill(renderBuffer.begin(), renderBuffer.end(), 0);
      return;
    }

    const uint32_t primary = colorFromSlot(state.colors[0]);
    const uint32_t secondary = colorFromSlot(state.colors[1]);
    const uint32_t tertiary = colorFromSlot(state.colors[2]);
    const std::string effectName = state.effect < effectCatalog.size() ? toLower(effectCatalog[state.effect].name) : "solid";
    const std::vector<HostCustomPaletteEntry> customPalettes = loadCustomPaletteEntries();
    const uint32_t speedOffset = (nowMs * std::max<uint8_t>(1U, state.speed)) / 32U;

    if (state.effect == 0 || effectName == "solid") {
      const uint32_t solid = scaleColor(primary, brightness);
      std::fill(renderBuffer.begin(), renderBuffer.end(), solid);
      return;
    }

    if (effectName.find("blink") != std::string::npos || effectName.find("strobe") != std::string::npos || effectName.find("flash") != std::string::npos) {
      const bool evenPhase = ((nowMs / std::max<uint16_t>(40U, 420U - state.speed)) & 1U) == 0;
      const uint32_t onColor = scaleColor(primary, brightness);
      const uint32_t offColor = scaleColor(secondary, static_cast<uint8_t>(brightness / 6U));
      for (uint16_t index = 0; index < ledCount; ++index) {
        const bool lit = evenPhase ? ((index & 1U) == 0U) : ((index & 1U) == 1U);
        renderBuffer[index] = lit ? onColor : offColor;
      }
      return;
    }

    if (effectName.find("chase") != std::string::npos || effectName.find("scan") != std::string::npos ||
        effectName.find("sweep") != std::string::npos || effectName.find("wipe") != std::string::npos ||
        effectName.find("comet") != std::string::npos || effectName.find("running") != std::string::npos) {
      const uint16_t head = static_cast<uint16_t>((speedOffset / std::max<uint16_t>(1U, 16U - state.intensity / 20U)) % ledCount);
      const uint8_t trailLength = std::max<uint8_t>(2U, static_cast<uint8_t>(2U + state.intensity / 28U));
      const uint32_t background = scaleColor(secondary ? secondary : tertiary, static_cast<uint8_t>(brightness / 10U));
      for (uint16_t index = 0; index < ledCount; ++index) {
        const uint16_t distance = static_cast<uint16_t>((index + ledCount - head) % ledCount);
        if (distance < trailLength) {
          const uint8_t fade = static_cast<uint8_t>(255U - (distance * 255U) / trailLength);
          const uint32_t headColor = scaleColor(samplePaletteColor(state, customPalettes, static_cast<uint8_t>((index * 255U) / std::max<uint16_t>(1U, ledCount - 1U))), brightness);
          renderBuffer[index] = mixColor(background, headColor, fade);
        } else {
          renderBuffer[index] = background;
        }
      }
      return;
    }

    if (effectName.find("sparkle") != std::string::npos || effectName.find("twinkle") != std::string::npos ||
        effectName.find("popcorn") != std::string::npos || effectName.find("firework") != std::string::npos) {
      const uint32_t background = scaleColor(primary, static_cast<uint8_t>(brightness / 7U));
      for (uint16_t index = 0; index < ledCount; ++index) {
        const uint32_t noise = (static_cast<uint32_t>(index) * 1103515245U) ^ speedOffset;
        const bool sparkle = (noise & 0xFFU) < std::max<uint8_t>(8U, state.intensity / 4U);
        const uint32_t sparkleColor = scaleColor(samplePaletteColor(state, customPalettes, static_cast<uint8_t>(noise >> 8)), brightness);
        renderBuffer[index] = sparkle ? sparkleColor : background;
      }
      return;
    }

    if (effectName.find("fade") != std::string::npos || effectName.find("breathe") != std::string::npos ||
        effectName.find("pulse") != std::string::npos) {
      const uint8_t wave = triangleWave((nowMs * std::max<uint8_t>(1U, state.speed / 3U + 1U)) / 24U);
      const uint32_t fill = scaleColor(mixColor(primary, samplePaletteColor(state, customPalettes, wave), wave), static_cast<uint8_t>((static_cast<uint16_t>(brightness) * (wave + 1U)) / 255U));
      std::fill(renderBuffer.begin(), renderBuffer.end(), fill);
      return;
    }

    for (uint16_t index = 0; index < ledCount; ++index) {
      const uint8_t paletteIndex = static_cast<uint8_t>(((static_cast<uint32_t>(index) * 255U) / std::max<uint16_t>(1U, ledCount - 1U)) + speedOffset + static_cast<uint32_t>(state.intensity) * ((index % 3U) + 1U));
      const uint32_t paletteColor = samplePaletteColor(state, customPalettes, paletteIndex);
      const uint8_t modulation = std::max<uint8_t>(32U, triangleWave(paletteIndex + static_cast<uint8_t>(nowMs / 18U)));
      renderBuffer[index] = scaleColor(paletteColor, static_cast<uint8_t>((static_cast<uint16_t>(brightness) * modulation) / 255U));
    }
  }

  std::string buildLiveViewFrame() {
    std::lock_guard<std::mutex> lock(stateMutex);
    renderFrameLocked(millis());
    const uint16_t ledCount = static_cast<uint16_t>(renderBuffer.size());

    std::string payload;
    payload.resize(2 + static_cast<size_t>(ledCount) * 3, '\0');
    payload[0] = 'L';
    payload[1] = 1;
    for (size_t index = 2; index < payload.size(); index += 3) {
      const uint32_t color = renderBuffer[(index - 2U) / 3U];
      const uint8_t white = static_cast<uint8_t>(color >> 24);
      payload[index] = static_cast<char>(std::min<uint16_t>(255U, static_cast<uint16_t>(static_cast<uint8_t>(color >> 16)) + white));
      payload[index + 1] = static_cast<char>(std::min<uint16_t>(255U, static_cast<uint16_t>(static_cast<uint8_t>(color >> 8)) + white));
      payload[index + 2] = static_cast<char>(std::min<uint16_t>(255U, static_cast<uint16_t>(static_cast<uint8_t>(color)) + white));
    }
    return payload;
  }

  std::string buildLiveJson() {
    std::lock_guard<std::mutex> lock(stateMutex);
    renderFrameLocked(millis());
    const uint16_t ledCount = static_cast<uint16_t>(renderBuffer.size());

    std::ostringstream output;
    output << "{\"leds\":[";
    for (uint16_t index = 0; index < ledCount; ++index) {
      if (index) output << ',';
      const uint32_t color = renderBuffer[index];
      const uint8_t white = static_cast<uint8_t>(color >> 24);
      const uint8_t red = static_cast<uint8_t>(std::min<uint16_t>(255U, static_cast<uint16_t>(static_cast<uint8_t>(color >> 16)) + white));
      const uint8_t green = static_cast<uint8_t>(std::min<uint16_t>(255U, static_cast<uint16_t>(static_cast<uint8_t>(color >> 8)) + white));
      const uint8_t blue = static_cast<uint8_t>(std::min<uint16_t>(255U, static_cast<uint16_t>(static_cast<uint8_t>(color)) + white));
      char colorHex[7] = {0};
      std::snprintf(colorHex, sizeof(colorHex), "%02X%02X%02X", red, green, blue);
      output << '"' << colorHex << '"';
    }
    output << "]}";
    return output.str();
  }

  std::string buildNetworkJson() {
    if (const char* injected = std::getenv("WLED_NATIVE_WIFI_SCAN_JSON"); injected && *injected) {
      DynamicJsonDocument doc(8192);
      if (!deserializeJson(doc, injected) && doc["networks"].is<JsonArray>()) {
        std::string output;
        serializeJson(doc, output);
        return output;
      }
    }

    DynamicJsonDocument doc(8192);
    JsonArray networks = doc.createNestedArray("networks");

#ifdef __APPLE__
    const std::string airportOutput = runCommandCapture("/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -s 2>/dev/null");
    std::istringstream stream(airportOutput);
    std::string line;
    bool skippedHeader = false;
    const std::regex airportPattern(R"(^(.*?)\s+([0-9A-Fa-f:]{17})\s+(-?\d+)\s+)");
    while (std::getline(stream, line)) {
      line = trimWhitespace(line);
      if (line.empty()) continue;
      if (!skippedHeader) {
        skippedHeader = true;
        continue;
      }
      std::smatch match;
      if (!std::regex_search(line, match, airportPattern)) continue;
      JsonObject network = networks.createNestedObject();
      network["ssid"] = trimWhitespace(match[1].str());
      network["bssid"] = match[2].str();
      network["rssi"] = std::atoi(match[3].str().c_str());
    }
#elif defined(__linux__)
    const std::string nmcliOutput = runCommandCapture("nmcli -t -f SSID,SIGNAL,BSSID dev wifi list --rescan no 2>/dev/null");
    std::istringstream stream(nmcliOutput);
    std::string line;
    while (std::getline(stream, line)) {
      if (line.empty()) continue;
      const size_t first = line.find(':');
      const size_t second = first == std::string::npos ? std::string::npos : line.find(':', first + 1);
      if (first == std::string::npos || second == std::string::npos) continue;
      JsonObject network = networks.createNestedObject();
      network["ssid"] = line.substr(0, first);
      network["rssi"] = std::atoi(line.substr(first + 1, second - first - 1).c_str()) - 100;
      network["bssid"] = line.substr(second + 1);
    }
#endif

    std::string output;
    serializeJson(doc, output);
    return output;
  }

  std::string buildPinsJson() {
    DynamicJsonDocument doc(16384);
    JsonArray pins = doc.createNestedArray("pins");
    for (int gpio = 0; gpio < 64; ++gpio) {
      JsonObject pin = pins.createNestedObject();
      pin["p"] = gpio;
      pin["c"] = 0;
      pin["a"] = false;
    }
    std::string output;
    serializeJson(doc, output);
    return output;
  }

  bool sendLiveViewFrame(const std::shared_ptr<WsClient>& client) {
    if (!client || client->fd < 0) return false;
    const std::string payload = buildLiveViewFrame();
    std::lock_guard<std::mutex> clientLock(client->writeMutex);
    return sendWsFrame(client->fd, 0x2, payload);
  }

  void setLiveViewClient(const std::shared_ptr<WsClient>& client) {
    std::lock_guard<std::mutex> lock(liveViewMutex);
    liveViewClient = client;
  }

  void clearLiveViewClient(const std::shared_ptr<WsClient>& client) {
    std::lock_guard<std::mutex> lock(liveViewMutex);
    const std::shared_ptr<WsClient> active = liveViewClient.lock();
    if (active && active == client) liveViewClient.reset();
  }

  void runLiveViewLoop() {
    uint64_t lastNodeRefreshMs = 0;
    while (!stopRequested.load()) {
      usermods.loop(*this);
      setHostMillis(static_cast<uint32_t>(millis()));
      const byte priorPreset = currentPreset;
      const int16_t priorPlaylist = currentPlaylist;
      handlePlaylist();
      bool playlistChanged = false;
      {
        std::lock_guard<std::mutex> lock(stateMutex);
        const bool playlistActive = currentPlaylist >= 0;
        if (state.playlistActive != playlistActive || state.playlist != currentPlaylist) {
          state.playlistActive = playlistActive;
          state.playlist = playlistActive ? currentPlaylist : -1;
          playlistChanged = true;
        }
      }
      if (currentPreset != priorPreset || (currentPlaylist != priorPlaylist && playlistChanged)) broadcastStateAsync();
      if (millis() - lastNodeRefreshMs >= 1000) {
        HostPersistedConfig configSnapshot;
        {
          std::lock_guard<std::mutex> configLock(configMutex);
          configSnapshot = persistedConfig;
        }
        if (configSnapshot.nodeBroadcastEnabled) refreshSelfNodeRegistryEntry();
        lastNodeRefreshMs = millis();
      }
      std::shared_ptr<WsClient> client;
      {
        std::lock_guard<std::mutex> lock(liveViewMutex);
        client = liveViewClient.lock();
      }
      if (client && !sendLiveViewFrame(client)) clearLiveViewClient(client);
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
  }

  bool writeStorageFile(const std::string& logicalPath, const std::string& body, std::string& error) {
    std::filesystem::path resolvedPath;
    if (!resolveHostStoragePath(options.storage, logicalPath, resolvedPath, error)) return false;

    std::ofstream output(resolvedPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      error = "Unable to open config-root file for writing: " + resolvedPath.string();
      return false;
    }

    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!output.good()) {
      error = "Unable to write config-root file: " + resolvedPath.string();
      return false;
    }

    return true;
  }

  bool stageUpdatePackage(const std::string& fileName, const std::string& body, std::string& error) {
    DynamicJsonDocument metadataDoc(1024);
    metadataDoc["name"] = fileName.c_str();
    metadataDoc["size"] = body.size();
    metadataDoc["staged"] = true;
    std::string metadataJson;
    serializeJson(metadataDoc, metadataJson);

    if (!writeStorageFile(kPendingUpdateFile, body, error)) return false;
    if (!writeStorageFile(kPendingUpdateMetaFile, metadataJson, error)) return false;
    return true;
  }

  bool revertStagedUpdate(std::string& error) {
    std::string ignoreError;
    deleteHostStorageFile(options.storage, kPendingUpdateFile, ignoreError);
    deleteHostStorageFile(options.storage, kPendingUpdateMetaFile, ignoreError);
    error.clear();
    return true;
  }

  StaticResponse handleEditRoute(const QueryParams& queryParams) {
    const auto funcIt = queryParams.find("func");
    std::string func;
    std::string requestedPath;
    if (funcIt != queryParams.end()) {
      func = funcIt->second;
      if (const auto pathIt = queryParams.find("path"); pathIt != queryParams.end()) requestedPath = pathIt->second;
    } else {
      for (const char* legacyFunc : {"list", "edit", "download", "delete"}) {
        const auto legacyIt = queryParams.find(legacyFunc);
        if (legacyIt == queryParams.end()) continue;
        func = legacyFunc;
        requestedPath = legacyIt->second;
        break;
      }
    }

    if (func.empty()) {
      std::filesystem::path assetPath;
      if (!resolveStaticPath("/edit", assetPath)) return makeTextResponse(404, "Not found\n");
      std::string body;
      if (!readStaticFile(assetPath, body)) return makeTextResponse(500, "Unable to read static asset\n");
      return {200, mimeTypeForPath(assetPath), body};
    }

    if ((func == "edit" || func == "download" || func == "delete") && requestedPath.empty()) {
      return makeTextResponse(400, "Missing path\n");
    }

    if (func == "list") {
      std::string error;
      std::ostringstream output;
      output << '[';
      bool first = true;
      for (const auto& entry : std::filesystem::directory_iterator(options.storage.configDir)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.find("wsec") != std::string::npos) continue;
        if (!first) output << ',';
        first = false;
        output << "{\"name\":" << jsonStringLiteral('/' + name)
               << ",\"type\":\"file\",\"size\":" << entry.file_size() << '}';
      }
      output << ']';
      return makeJsonResponse(output.str());
    }

    if (requestedPath.empty() || requestedPath[0] != '/') requestedPath.insert(requestedPath.begin(), '/');
    if (requestedPath.find("wsec") != std::string::npos) return makeTextResponse(403, "Access denied\n");

    if (func == "edit" || func == "download") {
      std::string content;
      std::string error;
      if (!readHostStorageFile(options.storage, requestedPath, content, error)) return makeTextResponse(404, "Not found\n");
      const std::string mimeType = mimeTypeForPath(std::filesystem::path(requestedPath));
      return {200, mimeType, content};
    }

    if (func == "delete") {
      std::string error;
      if (!deleteHostStorageFile(options.storage, requestedPath, error)) return makeTextResponse(500, "Delete failed\n");
      return makeTextResponse(200, "File deleted\n");
    }

    return makeTextResponse(400, "Invalid function\n");
  }

  bool prepareRuntime(std::string& error) {
    loadCatalogs();
    resetHostPlaylistState();
    setHostPlaylistApplyCallback([this](byte presetId) { return applyPresetSelection(presetId); });
    usermods.registerBuiltins();
    if (!refreshPersistedConfig(error)) return false;
    usermods.setup(*this);
    {
      std::lock_guard<std::mutex> configLock(configMutex);
      if (persistedConfig.bootPreset > 0 && !applyPresetSelection(persistedConfig.bootPreset)) {
        error = "Unable to apply configured boot preset";
        return false;
      }
    }
    return true;
  }

  bool inspectJsonTarget(const std::string& target, std::string& output, std::string& error) {
    if (target == "info") {
      output = buildInfoJson();
      return true;
    }
    if (target == "state") {
      output = buildStateJson();
      return true;
    }
    if (target == "si") {
      output = buildCombinedJson();
      return true;
    }
    if (target == "all") {
      output = buildAllJson();
      return true;
    }
    if (target == "effects") {
      output = effectsJson;
      return true;
    }
    if (target == "palettes") {
      output = palettesJson;
      return true;
    }
    if (target == "nodes") {
      output = buildNodesJson();
      return true;
    }
    if (target == "pal") {
      output = palettesJson;
      return true;
    }
    if (target == "cfg") {
      output = buildCfgJson();
      return true;
    }
    if (target == "net") {
      output = buildNetworkJson();
      return true;
    }
    if (target == "pins") {
      output = buildPinsJson();
      return true;
    }
    if (target == "live") {
      output = buildLiveJson();
      return true;
    }
    if (target.rfind("palx", 0) == 0) {
      int page = 0;
      const size_t separator = target.find(':');
      if (separator != std::string::npos) page = std::atoi(target.substr(separator + 1).c_str());
      output = buildPalettePageJson(page);
      return true;
    }
    error = "Unsupported JSON target: " + target;
    return false;
  }

  StaticResponse handleJsonRoute(const HttpRequest& request) {
    std::string logicalPath;
    QueryParams queryParams;
    splitPathAndQuery(request.path, logicalPath, queryParams);

    if (logicalPath == "/json/info" && request.method == "GET") return makeJsonResponse(buildInfoJson());
    if (logicalPath == "/json/state" && request.method == "GET") return makeJsonResponse(buildStateJson());
    if (logicalPath == "/json/si" && request.method == "GET") return makeJsonResponse(buildCombinedJson());
    if (logicalPath == "/json" && request.method == "GET") return makeJsonResponse(buildAllJson());
    if (logicalPath == "/json/effects" && request.method == "GET") return makeJsonResponse(effectsJson);
    if (logicalPath == "/json/palettes" && request.method == "GET") return makeJsonResponse(palettesJson);
    if (logicalPath == "/json/pal" && request.method == "GET") return makeJsonResponse(palettesJson);
    if (logicalPath == "/json/fxdata" && request.method == "GET") return makeJsonResponse(fxDataJson);
    if (logicalPath == "/json/live" && request.method == "GET") return makeJsonResponse(buildLiveJson());
    if (logicalPath == "/json/palx" && request.method == "GET") {
      int page = 0;
      if (const auto it = queryParams.find("page"); it != queryParams.end()) page = std::atoi(it->second.c_str());
      return makeJsonResponse(buildPalettePageJson(page));
    }
    if (logicalPath == "/json/cfg" && request.method == "GET") return makeJsonResponse(buildCfgJson());
    if (logicalPath == "/json/nodes" && request.method == "GET") return makeJsonResponse(buildNodesJson());
    if (logicalPath == "/json/pins" && request.method == "GET") return makeJsonResponse(buildPinsJson());
    if (logicalPath == "/json/net" && request.method == "GET") return makeJsonResponse(buildNetworkJson());

    if ((logicalPath == "/json/state" || logicalPath == "/json/si") && request.method == "POST") {
      DynamicJsonDocument doc(4096);
      const DeserializationError error = deserializeJson(doc, request.body);
      if (error) return makeTextResponse(400, std::string("Invalid JSON: ") + error.c_str() + "\n");
      std::string customPaletteError;
      if (doc["rmcpal"].is<uint8_t>() && !removeCustomPalette(doc["rmcpal"].as<uint8_t>(), customPaletteError)) {
        return makeTextResponse(400, customPaletteError + "\n");
      }
      std::string persistError;
      if (!savePresetFromJson(doc.as<JsonObjectConst>(), persistError)) return makeTextResponse(400, persistError.empty() ? "Unable to save preset\n" : persistError + "\n");
      if (doc["ps"].is<uint8_t>()) applyPresetSelection(doc["ps"].as<uint8_t>());
      if (doc["playlist"].is<JsonObjectConst>() && !loadPlaylistFromJson(doc["playlist"].as<JsonObjectConst>(), 0, doc["on"].isNull() ? true : doc["on"].as<bool>())) {
        return makeTextResponse(400, "Unable to load playlist\n");
      }
      applyStatePatch(doc.as<JsonVariantConst>());
      if (doc["pd"].is<uint8_t>()) {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.preset = doc["pd"].as<uint8_t>();
      }
      if (doc["np"].is<bool>() && doc["np"].as<bool>()) {
        doAdvancePlaylist = true;
        setHostMillis(static_cast<uint32_t>(millis()));
        handlePlaylist();
      }
      usermods.readFromJsonState(*this, doc.as<JsonObjectConst>());
      usermods.onStateChange(*this);
      broadcastStateAsync();
      return makeJsonResponse(buildCombinedJson());
    }

    return makeTextResponse(404, "Not found\n");
  }

  StaticResponse handleHttpRequest(const HttpRequest& request) {
    std::string logicalPath;
    QueryParams queryParams;
    splitPathAndQuery(request.path, logicalPath, queryParams);

    if (request.path.rfind("/json", 0) == 0) return handleJsonRoute(request);
    if (logicalPath == "/edit" && request.method == "GET") return handleEditRoute(queryParams);
    if (logicalPath == "/settings/s.js" && request.method == "GET") {
      int subPage = 0;
      if (const auto page = queryParams.find("p"); page != queryParams.end()) subPage = std::atoi(page->second.c_str());
      HostPersistedConfig configSnapshot;
      {
        std::lock_guard<std::mutex> configLock(configMutex);
        configSnapshot = persistedConfig;
      }
      return {200, "application/javascript; charset=utf-8", buildSettingsScript(subPage, configSnapshot, options)};
    }
    if (logicalPath == "/version" && request.method == "GET") return makeTextResponse(200, options.version + "\n");
    if (logicalPath == "/uptime" && request.method == "GET") return makeTextResponse(200, "0\n");
    if (logicalPath == "/reset" && request.method == "GET") {
      stopRequested.store(true);
      return {200, "text/html; charset=utf-8", "<!doctype html><html><body><p>Rebooting. Please wait a few seconds.</p></body></html>"};
    }
    if (logicalPath == "/update" && request.method == "GET" && queryParams.find("revert") != queryParams.end()) {
      std::string error;
      if (!revertStagedUpdate(error)) return makeTextResponse(500, error + "\n");
      return makeTextResponse(200, "Native update package reverted.\n");
    }
    if (logicalPath == "/update" && request.method == "POST") {
      std::string uploadPath;
      std::string uploadBody;
      if (!extractMultipartUpload(request, uploadPath, uploadBody)) return makeTextResponse(400, "Invalid multipart upload\n");
      std::string error;
      const std::string fileName = std::filesystem::path(uploadPath).filename().string();
      if (!stageUpdatePackage(fileName, uploadBody, error)) return makeTextResponse(400, error + "\n");
      return makeTextResponse(200, "Native update package staged. Restart and apply it with your host package workflow.\n");
    }
    if (logicalPath == "/updatebootloader" && request.method == "POST") return makeTextResponse(501, "Bootloader updates are not supported on native hosts.\n");
    if (logicalPath == "/upload" && request.method == "POST") {
      std::string uploadPath;
      std::string uploadBody;
      if (!extractMultipartUpload(request, uploadPath, uploadBody)) return makeTextResponse(400, "Invalid multipart upload\n");
      std::string error;
      if (!writeStorageFile(uploadPath, uploadBody, error)) return makeTextResponse(400, error + "\n");
      if (uploadPath == "/cfg.json" || uploadPath == "cfg.json") {
        if (!refreshPersistedConfig(error)) return makeTextResponse(400, error + "\n");
      }
      return makeTextResponse(200, "Upload completed.\n");
    }
    if (logicalPath.rfind("/settings", 0) == 0 && request.method == "POST") {
      const auto contentType = request.headers.find("content-type");
      if (contentType == request.headers.end() || contentType->second.find("application/x-www-form-urlencoded") == std::string::npos) {
        return makeTextResponse(400, "Unsupported settings payload\n");
      }
      std::string error;
      if (!persistSettings(logicalPath, parseFormUrlEncoded(request.body), error)) return makeTextResponse(400, error + "\n");
      return makeTextResponse(200, "Native settings saved.\n");
    }

    if (request.method != "GET") return makeTextResponse(405, "Method not allowed\n");

    if (logicalPath == "/skin.css") {
      std::filesystem::path assetPath;
      if (!resolveStaticPath(logicalPath, assetPath) && !resolveStorageAssetPath(options.storage, logicalPath, assetPath)) {
        return {200, "text/css; charset=utf-8", ""};
      }
      std::string body;
      if (!readStaticFile(assetPath, body)) return makeTextResponse(500, "Unable to read static asset\n");
      return {200, mimeTypeForPath(assetPath), body};
    }

    if (logicalPath == "/dmxmap") {
      std::filesystem::path assetPath;
      if (!resolveStaticPath(logicalPath, assetPath)) return makeTextResponse(404, "Not found\n");
      std::string body;
      if (!readStaticFile(assetPath, body)) return makeTextResponse(500, "Unable to read static asset\n");
      HostPersistedConfig configSnapshot;
      {
        std::lock_guard<std::mutex> configLock(configMutex);
        configSnapshot = persistedConfig;
      }
      const std::string varsScript = "<script>" + buildDmxMapVarsScript(configSnapshot) + "</script>";
      const size_t headEnd = body.find("</head>");
      if (headEnd != std::string::npos) body.insert(headEnd, varsScript);
      else body.insert(0, varsScript);
      return {200, mimeTypeForPath(assetPath), body};
    }

    std::filesystem::path assetPath;
    if (!resolveStaticPath(logicalPath, assetPath) && !resolveStorageAssetPath(options.storage, logicalPath, assetPath)) {
      return makeTextResponse(404, "Not found\n");
    }

    std::string body;
    if (!readStaticFile(assetPath, body)) return makeTextResponse(500, "Unable to read static asset\n");
    return {200, mimeTypeForPath(assetPath), body};
  }

  bool handleWebSocketHandshake(int fd, const HttpRequest& request) {
    const auto upgrade = request.headers.find("upgrade");
    const auto key = request.headers.find("sec-websocket-key");
    if (upgrade == request.headers.end() || key == request.headers.end() || toLower(upgrade->second) != "websocket") return false;

    std::ostringstream response;
    response
      << "HTTP/1.1 101 Switching Protocols\r\n"
      << "Upgrade: websocket\r\n"
      << "Connection: Upgrade\r\n"
      << "Sec-WebSocket-Accept: " << websocketAcceptKey(key->second) << "\r\n"
      << "\r\n";
    return sendString(fd, response.str());
  }

  void handleWsClient(std::shared_ptr<WsClient> client) {
    {
      std::lock_guard<std::mutex> clientLock(client->writeMutex);
      sendWsFrame(client->fd, 0x1, buildCombinedJson());
    }

    while (!stopRequested.load()) {
      struct pollfd pollFd { client->fd, POLLIN, 0 };
      const int ready = poll(&pollFd, 1, 200);
      if (ready < 0) break;
      if (ready == 0) continue;

      std::string payload;
      uint8_t opcode = 0;
      if (!readWsFrame(client->fd, payload, opcode)) break;
      if (opcode == 0x8) break;
      if (opcode == 0x9) {
        std::lock_guard<std::mutex> clientLock(client->writeMutex);
        if (!sendWsFrame(client->fd, 0xA, payload)) break;
        continue;
      }
      if (opcode != 0x1) continue;

      if (payload == "p") {
        std::lock_guard<std::mutex> clientLock(client->writeMutex);
        if (!sendWsFrame(client->fd, 0x1, "pong")) break;
        continue;
      }

      DynamicJsonDocument doc(4096);
      const DeserializationError error = deserializeJson(doc, payload);
      if (error) continue;
      if (doc["v"].is<bool>() && doc["v"].as<bool>() && doc.size() == 1) {
        std::lock_guard<std::mutex> clientLock(client->writeMutex);
        if (!sendWsFrame(client->fd, 0x1, buildCombinedJson())) break;
        continue;
      }
      if (doc.containsKey("lv")) {
        if (doc["lv"].as<bool>()) {
          setLiveViewClient(client);
        } else {
          clearLiveViewClient(client);
        }
        std::lock_guard<std::mutex> clientLock(client->writeMutex);
        if (!sendWsFrame(client->fd, 0x1, "{\"success\":true}")) break;
        continue;
      }
      std::string customPaletteError;
      if (doc["rmcpal"].is<uint8_t>() && !removeCustomPalette(doc["rmcpal"].as<uint8_t>(), customPaletteError)) continue;
      std::string persistError;
      if (!savePresetFromJson(doc.as<JsonObjectConst>(), persistError)) continue;
      if (doc["ps"].is<uint8_t>()) applyPresetSelection(doc["ps"].as<uint8_t>());
      if (doc["playlist"].is<JsonObjectConst>()) loadPlaylistFromJson(doc["playlist"].as<JsonObjectConst>(), 0, doc["on"].isNull() ? true : doc["on"].as<bool>());
      applyStatePatch(doc.as<JsonVariantConst>());
      if (doc["pd"].is<uint8_t>()) {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.preset = doc["pd"].as<uint8_t>();
      }
      if (doc["np"].is<bool>() && doc["np"].as<bool>()) {
        doAdvancePlaylist = true;
        setHostMillis(static_cast<uint32_t>(millis()));
        handlePlaylist();
      }
      usermods.readFromJsonState(*this, doc.as<JsonObjectConst>());
      usermods.onStateChange(*this);
      broadcastState();
    }

    clearLiveViewClient(client);
    {
      std::lock_guard<std::mutex> clientLock(client->writeMutex);
      sendWsFrame(client->fd, 0x8, std::string("\x03\xE8", 2));
    }
    close(client->fd);
    client->fd = -1;
  }

  void handleConnection(int fd) {
    HttpRequest request;
    std::string error;
    if (!readHttpRequest(fd, request, error)) {
      writeHttpResponse(fd, makeTextResponse(400, error + "\n"));
      close(fd);
      return;
    }

    if (request.path == "/ws") {
      if (!handleWebSocketHandshake(fd, request)) {
        writeHttpResponse(fd, makeTextResponse(400, "Invalid WebSocket handshake\n"));
        close(fd);
        return;
      }

      auto client = std::make_shared<WsClient>();
      client->fd = fd;
      {
        std::lock_guard<std::mutex> lock(clientsMutex);
        wsClients.push_back(client);
      }
      handleWsClient(client);
      return;
    }

    writeHttpResponse(fd, handleHttpRequest(request));
    close(fd);
  }
};

HostServer::HostServer() : impl_(new Impl()) {}

HostServer::~HostServer() {
  stop();
  delete impl_;
}

bool HostServer::start(const HostServerOptions& options, std::string& error) {
  impl_->options = options;
  impl_->startTime = std::chrono::steady_clock::now();
  if (!impl_->prepareRuntime(error)) {
    setHostPlaylistApplyCallback(nullptr);
    return false;
  }
  impl_->listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (impl_->listenFd < 0) {
    error = std::string("Unable to create native server socket: ") + std::strerror(errno);
    return false;
  }

  int reuse = 1;
  setsockopt(impl_->listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(options.port));
  if (inet_pton(AF_INET, options.host.c_str(), &address.sin_addr) != 1) {
    error = "Only numeric IPv4 host addresses are supported right now: " + options.host;
    close(impl_->listenFd);
    impl_->listenFd = -1;
    return false;
  }

  if (bind(impl_->listenFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    error = std::string("Unable to bind native server socket: ") + std::strerror(errno);
    close(impl_->listenFd);
    impl_->listenFd = -1;
    return false;
  }

  if (listen(impl_->listenFd, 16) != 0) {
    error = std::string("Unable to listen on native server socket: ") + std::strerror(errno);
    close(impl_->listenFd);
    impl_->listenFd = -1;
    return false;
  }

  sockaddr_in boundAddress {};
  socklen_t boundLength = sizeof(boundAddress);
  if (getsockname(impl_->listenFd, reinterpret_cast<sockaddr*>(&boundAddress), &boundLength) != 0) {
    error = std::string("Unable to query native server socket address: ") + std::strerror(errno);
    close(impl_->listenFd);
    impl_->listenFd = -1;
    return false;
  }

  impl_->actualPort = ntohs(boundAddress.sin_port);
  impl_->stopRequested.store(false);
  bool nodeBroadcastEnabled = true;
  {
    std::lock_guard<std::mutex> configLock(impl_->configMutex);
    nodeBroadcastEnabled = impl_->persistedConfig.nodeBroadcastEnabled;
  }
  if (nodeBroadcastEnabled) impl_->refreshSelfNodeRegistryEntry();
  impl_->workers.emplace_back([this]() { impl_->runLiveViewLoop(); });
  return true;
}

bool HostServer::inspectJson(const HostServerOptions& options, const std::string& target, std::string& output, std::string& error) {
  impl_->options = options;
  impl_->startTime = std::chrono::steady_clock::now();
  if (!impl_->prepareRuntime(error)) {
    setHostPlaylistApplyCallback(nullptr);
    return false;
  }
  const bool ok = impl_->inspectJsonTarget(target, output, error);
  setHostPlaylistApplyCallback(nullptr);
  return ok;
}

bool HostServer::renderRoute(const HostServerOptions& options, const std::string& path, std::string& contentType, std::string& output, std::string& error) {
  impl_->options = options;
  impl_->startTime = std::chrono::steady_clock::now();
  if (!impl_->prepareRuntime(error)) {
    setHostPlaylistApplyCallback(nullptr);
    return false;
  }
  HttpRequest request;
  request.method = "GET";
  request.path = path;
  const StaticResponse response = impl_->handleHttpRequest(request);
  setHostPlaylistApplyCallback(nullptr);
  if (response.status != 200) {
    error = response.body.empty() ? ("Route returned HTTP " + std::to_string(response.status)) : response.body;
    return false;
  }
  contentType = response.contentType;
  output = response.body;
  return true;
}

bool HostServer::applySettings(const HostServerOptions& options, const std::string& logicalPath, const std::string& encodedBody, std::string& error) {
  impl_->options = options;
  impl_->startTime = std::chrono::steady_clock::now();
  if (!impl_->prepareRuntime(error)) {
    setHostPlaylistApplyCallback(nullptr);
    return false;
  }
  const bool ok = impl_->persistSettings(logicalPath, parseFormUrlEncoded(encodedBody), error);
  setHostPlaylistApplyCallback(nullptr);
  return ok;
}

bool HostServer::applyJson(const HostServerOptions& options, const std::string& logicalPath, const std::string& body, std::string& output, std::string& error) {
  impl_->options = options;
  impl_->startTime = std::chrono::steady_clock::now();
  if (!impl_->prepareRuntime(error)) {
    setHostPlaylistApplyCallback(nullptr);
    return false;
  }
  HttpRequest request;
  request.method = "POST";
  request.path = logicalPath;
  request.headers["content-type"] = "application/json";
  request.body = body;
  const StaticResponse response = impl_->handleHttpRequest(request);
  setHostPlaylistApplyCallback(nullptr);
  if (response.status != 200) {
    error = response.body.empty() ? ("Route returned HTTP " + std::to_string(response.status)) : response.body;
    return false;
  }
  output = response.body;
  return true;
}

bool HostServer::stageUpdate(const HostServerOptions& options, const std::string& fileName, const std::string& body, std::string& error) {
  impl_->options = options;
  impl_->startTime = std::chrono::steady_clock::now();
  if (!impl_->prepareRuntime(error)) {
    setHostPlaylistApplyCallback(nullptr);
    return false;
  }
  const bool ok = impl_->stageUpdatePackage(fileName, body, error);
  setHostPlaylistApplyCallback(nullptr);
  return ok;
}

int HostServer::port() const {
  return impl_->actualPort;
}

std::string HostServer::listeningUrl() const {
  std::ostringstream output;
  output << "http://" << impl_->options.host << ':' << impl_->actualPort;
  return output.str();
}

void HostServer::runUntilStopped(const std::atomic<bool>& externalStopRequested) {
  while (!impl_->stopRequested.load() && !externalStopRequested.load()) {
    struct pollfd pollFd { impl_->listenFd, POLLIN, 0 };
    const int ready = poll(&pollFd, 1, 200);
    if (ready < 0) break;
    if (ready == 0) continue;

    sockaddr_in clientAddress {};
    socklen_t clientLength = sizeof(clientAddress);
    const int clientFd = accept(impl_->listenFd, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
    if (clientFd < 0) continue;
    impl_->workers.emplace_back([this, clientFd]() { impl_->handleConnection(clientFd); });
  }
  stop();
}

void HostServer::stop() {
  if (!impl_) return;
  const bool wasRequested = impl_->stopRequested.exchange(true);
  setHostPlaylistApplyCallback(nullptr);
  impl_->removeSelfNodeRegistryEntry();
  if (!wasRequested && impl_->listenFd >= 0) {
    shutdown(impl_->listenFd, SHUT_RDWR);
    close(impl_->listenFd);
    impl_->listenFd = -1;
  }

  {
    std::lock_guard<std::mutex> lock(impl_->clientsMutex);
    for (const std::shared_ptr<WsClient>& client : impl_->wsClients) {
      if (client && client->fd >= 0) shutdown(client->fd, SHUT_RDWR);
    }
  }

  for (std::thread& worker : impl_->workers) {
    if (worker.joinable()) worker.join();
  }
  impl_->workers.clear();
}

#endif // !ARDUINO
