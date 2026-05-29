#ifndef WLED_HOST_FILE_H
#define WLED_HOST_FILE_H

#ifndef ARDUINO

#include "src/dependencies/json/ArduinoJson-v6.h"

bool writeObjectToFileUsingId(const char* file, uint16_t id, const JsonDocument* content);
bool writeObjectToFile(const char* file, const char* key, const JsonDocument* content);
bool readObjectFromFileUsingId(const char* file, uint16_t id, JsonDocument* dest, const JsonDocument* filter = nullptr);
bool readObjectFromFile(const char* file, const char* key, JsonDocument* dest, const JsonDocument* filter = nullptr);
void updateFSInfo();
void closeFile();
bool copyFile(const char* srcPath, const char* dstPath);
bool backupFile(const char* filename);
bool restoreFile(const char* filename);
bool checkBackupExists(const char* filename);
bool validateJsonFile(const char* filename);
void dumpFilesToSerial();

#endif

#endif // WLED_HOST_FILE_H
