#ifndef WLED_HOST_CLI_H
#define WLED_HOST_CLI_H

#include <ostream>
#include <string>
#include <vector>

struct HostCliOptions {
  bool showHelp = false;
  bool showVersion = false;
  std::string configDir;
  std::string host = "127.0.0.1";
  std::string logLevel = "info";
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
