#ifndef WLED_HOST_CLI_H
#define WLED_HOST_CLI_H

#include <ostream>
#include <string>
#include <vector>

struct HostCliOptions {
  bool showHelp = false;
  bool showVersion = false;
  bool exitAfterBootstrap = false;
  std::string configDir;
  std::string host = "127.0.0.1";
  std::string logLevel = "info";
  std::string resolvePath;
  std::string readPath;
  std::string copyPathSpec;
  std::string renamePathSpec;
  std::string deletePath;
  std::string comparePathSpec;
  std::string validatePath;
  std::string backupPath;
  std::string restorePath;
  std::string hasBackupPath;
  std::string blendColorSpec;
  std::string addColorSpec;
  std::string fadeColorSpec;
  std::string prngSequenceSpec;
  std::string playlistRunSpec;
  std::string presetNameSpec;
  std::string deletePresetSpec;
  bool initPresets = false;
  bool backupConfig = false;
  bool restoreConfig = false;
  bool verifyConfig = false;
  bool resetConfig = false;
  bool hasConfigBackup = false;
  bool verifySecrets = false;
  bool listFiles = false;
  int port = 21324;
  std::vector<std::string> positionalArgs;
};

struct HostCliParseResult {
  bool ok = true;
  HostCliOptions options;
  std::string error;
};

HostCliParseResult parseHostCliArgs(int argc, char** argv);
void printHostCliHelp(std::ostream& out);
void printHostCliVersion(std::ostream& out);

#endif // WLED_HOST_CLI_H
