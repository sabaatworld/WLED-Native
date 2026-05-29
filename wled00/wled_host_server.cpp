#ifndef ARDUINO

#include "wled_host_server.h"

#include "wled_host_storage.h"
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
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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

struct HostRuntimeState {
  bool on = true;
  uint8_t bri = 128;
  uint16_t transition = 7;
  uint8_t preset = 0;
  int16_t playlist = -1;
  uint8_t effect = 0;
  uint8_t palette = 0;
  uint16_t ledCount = 30;
};

struct HttpRequest {
  std::string method;
  std::string path;
  std::map<std::string, std::string> headers;
  std::string body;
};

struct StaticResponse {
  int status = 200;
  std::string contentType;
  std::string body;
};

struct WsClient {
  int fd = -1;
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

std::string buildMinimalCfgJson() {
  DynamicJsonDocument doc(1024);
  doc["comp"]["on"] = 0;
  doc["comp"]["off"] = 0;
  doc["hw"]["led"]["cr"] = false;
  doc["hw"]["led"]["cct"] = false;
  JsonArray ledIns = doc["hw"]["led"].createNestedArray("ins");
  JsonObject led = ledIns.createNestedObject();
  led["type"] = 22;
  doc["hw"]["relay"]["pin"] = -1;
  doc["hw"]["ir"]["type"] = 0;
  doc["light"]["gc"]["col"] = 1.0;
  doc["light"]["gc"]["bri"] = 1.0;
  doc["light"]["aseg"] = false;
  doc["light"]["nl"]["mode"] = 0;
  doc["if"]["hue"]["en"] = false;
  doc["if"]["mqtt"]["en"] = false;
  doc["if"]["va"]["alexa"] = false;
  doc["if"]["sync"]["send"]["en"] = false;
  doc["if"]["sync"]["espnow"] = false;
  doc["nw"]["espnow"] = false;
  doc.createNestedObject("um");

  std::string output;
  serializeJson(doc, output);
  return output;
}

std::filesystem::path repoDataRoot() {
  return std::filesystem::path(WLED_HOST_REPO_ROOT) / "wled00" / "data";
}

bool resolveStaticPath(const std::string& requestPath, std::filesystem::path& resolvedPath) {
  std::string logicalPath = requestPath;
  const size_t querySeparator = logicalPath.find('?');
  if (querySeparator != std::string::npos) logicalPath.resize(querySeparator);

  if (logicalPath.empty() || logicalPath == "/") logicalPath = "/index.htm";
  else if (logicalPath == "/settings") logicalPath = "/settings.htm";
  else if (logicalPath == "/update") logicalPath = "/update.htm";

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

bool readStaticFile(const std::filesystem::path& path, std::string& body) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) return false;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  body = buffer.str();
  return true;
}

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

struct HostServer::Impl {
  HostServerOptions options;
  HostRuntimeState state;
  int listenFd = -1;
  int actualPort = -1;
  std::atomic<bool> stopRequested{false};
  std::mutex stateMutex;
  std::mutex clientsMutex;
  std::vector<std::shared_ptr<WsClient>> wsClients;
  std::vector<std::thread> workers;

  std::string buildInfoJson() {
    DynamicJsonDocument doc(2048);
    doc["brand"] = options.productName.c_str();
    doc["product"] = options.productName.c_str();
    doc["name"] = options.productName.c_str();
    doc["ver"] = options.version.c_str();
    doc["vid"] = 1700000;
    doc["release"] = "WLED_Native";
    doc["repo"] = "wled-dev/WLED";
    doc["arch"] = "native";
    doc["core"] = "native";
    doc["mac"] = options.instanceId.c_str();
    doc["live"] = false;
    doc["liveseg"] = -1;
    doc["fxcount"] = 1;
    doc["palcount"] = 1;
    doc["ws"] = 1;
    doc["ndc"] = 1;
    doc["fs"]["u"] = 0;
    doc["fs"]["t"] = 0;

    JsonObject leds = doc.createNestedObject("leds");
    leds["count"] = state.ledCount;
    leds["rgbw"] = false;
    leds["wv"] = 0;
    leds["fps"] = 0;
    leds["pwr"] = 0;
    leds["maxpwr"] = 0;
    leds["lc"] = 1;
    JsonArray seglc = leds.createNestedArray("seglc");
    seglc.add(state.ledCount);

    std::string output;
    serializeJson(doc, output);
    return output;
  }

  std::string buildStateJson() {
    DynamicJsonDocument doc(2048);
    std::lock_guard<std::mutex> lock(stateMutex);

    doc["on"] = state.on;
    doc["bri"] = state.bri;
    doc["transition"] = state.transition;
    doc["ps"] = state.preset;
    doc["pl"] = state.playlist;
    doc["lor"] = 0;
    doc["mainseg"] = 0;
    doc["nl"]["on"] = false;
    doc["nl"]["dur"] = 0;
    doc["nl"]["mode"] = 0;
    doc["nl"]["tbri"] = 0;
    doc["nl"]["rem"] = 0;

    JsonArray segments = doc.createNestedArray("seg");
    JsonObject segment = segments.createNestedObject();
    segment["id"] = 0;
    segment["start"] = 0;
    segment["stop"] = state.ledCount;
    segment["len"] = state.ledCount;
    segment["grp"] = 1;
    segment["spc"] = 0;
    segment["of"] = 0;
    segment["on"] = state.on;
    segment["bri"] = 255;
    segment["fx"] = state.effect;
    segment["sx"] = 128;
    segment["ix"] = 128;
    segment["pal"] = state.palette;
    segment["sel"] = true;
    segment["rev"] = false;
    segment["mi"] = false;
    JsonArray colors = segment.createNestedArray("col");
    JsonArray primary = colors.createNestedArray();
    primary.add(255);
    primary.add(160);
    primary.add(0);
    JsonArray secondary = colors.createNestedArray();
    secondary.add(0);
    secondary.add(0);
    secondary.add(0);
    JsonArray tertiary = colors.createNestedArray();
    tertiary.add(0);
    tertiary.add(0);
    tertiary.add(0);

    std::string output;
    serializeJson(doc, output);
    return output;
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

  void applyStatePatch(JsonVariantConst patch) {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (patch["on"].is<bool>()) state.on = patch["on"].as<bool>();
    if (patch["bri"].is<uint8_t>()) state.bri = patch["bri"].as<uint8_t>();
    if (patch["transition"].is<uint16_t>()) state.transition = patch["transition"].as<uint16_t>();
    if (patch["ps"].is<uint8_t>()) state.preset = patch["ps"].as<uint8_t>();
    if (patch["pl"].is<int>()) state.playlist = patch["pl"].as<int16_t>();

    if (patch["seg"].is<JsonArrayConst>()) {
      JsonArrayConst segments = patch["seg"].as<JsonArrayConst>();
      for (JsonVariantConst segmentValue : segments) {
        if (!segmentValue.is<JsonObjectConst>()) continue;
        JsonObjectConst segment = segmentValue.as<JsonObjectConst>();
        if (segment["on"].is<bool>()) state.on = segment["on"].as<bool>();
        if (segment["fx"].is<uint8_t>()) state.effect = segment["fx"].as<uint8_t>();
        if (segment["pal"].is<uint8_t>()) state.palette = segment["pal"].as<uint8_t>();
      }
    }
  }

  void broadcastState() {
    const std::string payload = buildCombinedJson();
    std::lock_guard<std::mutex> lock(clientsMutex);
    auto it = wsClients.begin();
    while (it != wsClients.end()) {
      const std::shared_ptr<WsClient>& client = *it;
      if (!client || client->fd < 0 || !sendWsFrame(client->fd, 0x1, payload)) {
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

  StaticResponse handleJsonRoute(const HttpRequest& request) {
    if (request.path == "/json/info" && request.method == "GET") return makeJsonResponse(buildInfoJson());
    if (request.path == "/json/state" && request.method == "GET") return makeJsonResponse(buildStateJson());
    if ((request.path == "/json" || request.path == "/json/si") && request.method == "GET") return makeJsonResponse(buildCombinedJson());
    if (request.path == "/json/effects" && request.method == "GET") return makeJsonResponse("[\"Solid\"]");
    if (request.path == "/json/palettes" && request.method == "GET") return makeJsonResponse("[\"Default\"]");
    if (request.path == "/json/fxdata" && request.method == "GET") return makeJsonResponse("[\"\"]");
    if (request.path == "/json/cfg" && request.method == "GET") return makeJsonResponse(buildMinimalCfgJson());
    if (request.path == "/json/nodes" && request.method == "GET") return makeJsonResponse("{}");
    if (request.path == "/json/pins" && request.method == "GET") return makeJsonResponse("[]");

    if ((request.path == "/json/state" || request.path == "/json/si") && request.method == "POST") {
      DynamicJsonDocument doc(4096);
      const DeserializationError error = deserializeJson(doc, request.body);
      if (error) return makeTextResponse(400, std::string("Invalid JSON: ") + error.c_str() + "\n");
      applyStatePatch(doc.as<JsonVariantConst>());
      broadcastStateAsync();
      return makeJsonResponse(buildCombinedJson());
    }

    return makeTextResponse(404, "Not found\n");
  }

  StaticResponse handleHttpRequest(const HttpRequest& request) {
    if (request.path.rfind("/json", 0) == 0) return handleJsonRoute(request);
    if (request.path == "/version" && request.method == "GET") return makeTextResponse(200, options.version + "\n");
    if (request.path == "/uptime" && request.method == "GET") return makeTextResponse(200, "0\n");
    if (request.path == "/update" && request.method == "POST") return makeTextResponse(501, "Native package updates are not implemented yet.\n");

    if (request.method != "GET") return makeTextResponse(405, "Method not allowed\n");

    std::filesystem::path staticPath;
    if (!resolveStaticPath(request.path, staticPath)) return makeTextResponse(404, "Not found\n");

    std::string body;
    if (!readStaticFile(staticPath, body)) return makeTextResponse(500, "Unable to read static asset\n");
    return {200, mimeTypeForPath(staticPath), body};
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
    sendWsFrame(client->fd, 0x1, buildCombinedJson());

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
        if (!sendWsFrame(client->fd, 0xA, payload)) break;
        continue;
      }
      if (opcode != 0x1) continue;

      DynamicJsonDocument doc(4096);
      const DeserializationError error = deserializeJson(doc, payload);
      if (error) continue;
      applyStatePatch(doc.as<JsonVariantConst>());
      broadcastState();
    }

    sendWsFrame(client->fd, 0x8, std::string("\x03\xE8", 2));
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
  return true;
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
