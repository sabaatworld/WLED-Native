#ifndef WLED_HOST_STORAGE_H
#define WLED_HOST_STORAGE_H

#include <filesystem>
#include <string>
#include <vector>

struct HostStorageLayout {
  std::filesystem::path configDir;
  std::filesystem::path instanceIdFile;
  std::filesystem::path cfgFile;
  std::filesystem::path wsecFile;
  std::filesystem::path presetsFile;
  std::filesystem::path tempPresetFile;
};

std::filesystem::path resolveHostConfigDirectory(const std::string& cliOverride);
HostStorageLayout buildHostStorageLayout(const std::filesystem::path& configDir);
bool bootstrapHostStorage(const HostStorageLayout& layout, std::string& instanceId, std::string& error);
bool resolveHostStoragePath(const HostStorageLayout& layout, const std::string& logicalPath, std::filesystem::path& resolvedPath, std::string& error);
void setActiveHostStorageLayout(const HostStorageLayout* layout);
const HostStorageLayout* getActiveHostStorageLayout();
bool readHostStorageFile(const HostStorageLayout& layout, const std::string& logicalPath, std::string& content, std::string& error);
bool copyHostStorageFile(const HostStorageLayout& layout, const std::string& sourceLogicalPath, const std::string& destinationLogicalPath, std::string& error);
bool renameHostStorageFile(const HostStorageLayout& layout, const std::string& sourceLogicalPath, const std::string& destinationLogicalPath, std::string& error);
bool deleteHostStorageFile(const HostStorageLayout& layout, const std::string& logicalPath, std::string& error);
bool compareHostStorageFiles(const HostStorageLayout& layout, const std::string& firstLogicalPath, const std::string& secondLogicalPath, bool& identical, std::string& error);
bool validateHostStorageJsonFile(const HostStorageLayout& layout, const std::string& logicalPath, std::string& error);
bool createHostStorageBackup(const HostStorageLayout& layout, const std::string& logicalPath, std::string& backupLogicalPath, std::string& error);
bool restoreHostStorageBackup(const HostStorageLayout& layout, const std::string& logicalPath, std::string& backupLogicalPath, std::string& error);
bool hostStorageBackupExists(const HostStorageLayout& layout, const std::string& logicalPath, std::string& backupLogicalPath, std::string& error);
bool listHostStorageFiles(const HostStorageLayout& layout, std::vector<std::string>& fileNames, std::string& error);

#endif // WLED_HOST_STORAGE_H
