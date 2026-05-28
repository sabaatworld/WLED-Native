#pragma once

class NativeRuntime;

class NativeSignalHandler {
public:
  explicit NativeSignalHandler(NativeRuntime &runtime);
  ~NativeSignalHandler();

  bool install();
  void restore();

private:
  NativeRuntime &_runtime;
  bool _installed = false;
};

bool nativeSignalStopRequested();
void nativeResetSignalState();
