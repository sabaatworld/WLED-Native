#ifndef WLED_HOST_PRESETS_H
#define WLED_HOST_PRESETS_H

#ifndef ARDUINO

#include <string>

#include "wled_host_file.h"
#include "wled_host_storage.h"
#include "colors.h"

using String = std::string;

const char* getPresetsFileName(bool persistent = true);
bool presetNeedsSaving();
void initPresetsFile();
bool getPresetName(byte index, String& name);
void deletePreset(byte index);

#endif

#endif // WLED_HOST_PRESETS_H
