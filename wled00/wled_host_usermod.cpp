#ifndef ARDUINO

#include "wled_host_usermod.h"

#include <memory>

std::unique_ptr<HostUsermod> createAutoSaveHostUsermod();

void HostUsermodManager::registerBuiltins() {
  if (!usermods_.empty()) return;
  usermods_.push_back(createAutoSaveHostUsermod());
}

void HostUsermodManager::setup(HostUsermodContext& context) {
  if (setupDone_) return;
  for (const std::unique_ptr<HostUsermod>& usermod : usermods_) usermod->setup(context);
  setupDone_ = true;
}

void HostUsermodManager::loop(HostUsermodContext& context) {
  for (const std::unique_ptr<HostUsermod>& usermod : usermods_) usermod->loop(context);
}

void HostUsermodManager::addToJsonState(HostUsermodContext& context, JsonObject& root) {
  for (const std::unique_ptr<HostUsermod>& usermod : usermods_) usermod->addToJsonState(context, root);
}

void HostUsermodManager::addToJsonInfo(HostUsermodContext& context, JsonObject& root) {
  JsonArray ids = root["um"].is<JsonArray>() ? root["um"].as<JsonArray>() : root.createNestedArray("um");
  for (const std::unique_ptr<HostUsermod>& usermod : usermods_) {
    ids.add(usermod->getId());
    usermod->addToJsonInfo(context, root);
  }
}

void HostUsermodManager::readFromJsonState(HostUsermodContext& context, JsonObjectConst root) {
  for (const std::unique_ptr<HostUsermod>& usermod : usermods_) usermod->readFromJsonState(context, root);
}

void HostUsermodManager::addToConfig(JsonObject& root) {
  for (const std::unique_ptr<HostUsermod>& usermod : usermods_) usermod->addToConfig(root);
}

bool HostUsermodManager::readFromConfig(JsonObjectConst root) {
  bool complete = true;
  for (const std::unique_ptr<HostUsermod>& usermod : usermods_) {
    if (!usermod->readFromConfig(root)) complete = false;
  }
  return complete;
}

void HostUsermodManager::onStateChange(HostUsermodContext& context) {
  for (const std::unique_ptr<HostUsermod>& usermod : usermods_) usermod->onStateChange(context);
}

#endif
