#include "runtime/Runtime.h"
#include "runtime/SignalHandling.h"

#include <iostream>

int main(int argc, char **argv) {
  NativeCliResult cli;
  if (!parseNativeCli(argc, argv, cli, std::cout, std::cerr)) {
    std::cerr << "Run 'wled-native --help' for usage.\n";
    return 64;
  }

  if (cli.exitNow) return cli.exitCode;

  NativeRuntime runtime(cli.options);
  NativeSignalHandler signals(runtime);
  if (!signals.install()) std::cerr << "Warning: signal handling is not available\n";

  const int exitCode = runtime.run();
  signals.restore();
  return exitCode;
}
