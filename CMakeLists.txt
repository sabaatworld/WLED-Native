cmake_minimum_required(VERSION 3.20)

project(WLEDNative LANGUAGES CXX)

set(WLED_NATIVE_VERSION "17.0.0-dev")

# Use C++17 for all targets
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(arduino_compat
  native/arduino_compat/Arduino.cpp
  native/arduino_compat/Print.cpp
  native/arduino_compat/WString.cpp
  native/arduino_compat/IPAddress.cpp
)
target_include_directories(arduino_compat PUBLIC native/arduino_compat)

add_library(wled_fs
  native/fs/LittleFS.cpp
)
target_include_directories(wled_fs PUBLIC native/fs)
target_link_libraries(wled_fs PUBLIC arduino_compat)

add_library(native_net
  native/net/Network.cpp
)
target_include_directories(native_net PUBLIC native/net)
target_link_libraries(native_net PUBLIC arduino_compat)
target_compile_features(native_net PRIVATE cxx_std_17)

add_library(native_runtime
  native/runtime/Runtime.cpp
  native/runtime/SignalHandling.cpp
)
target_include_directories(native_runtime PUBLIC native/runtime)
target_link_libraries(native_runtime PUBLIC arduino_compat native_net)
target_compile_features(native_runtime PRIVATE cxx_std_17)
target_compile_definitions(native_runtime PUBLIC WLED_NATIVE_VERSION="${WLED_NATIVE_VERSION}")

add_executable(wled-native
  native/main.cpp
)
target_link_libraries(wled-native PRIVATE wled_fs native_runtime)
target_compile_features(wled-native PRIVATE cxx_std_17)
target_compile_definitions(wled-native PRIVATE WLED_NATIVE_VERSION="${WLED_NATIVE_VERSION}")

target_compile_features(wled_fs PRIVATE cxx_std_17)

include(CTest)
if(BUILD_TESTING)
  add_test(
    NAME native_cli_smoke
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/cli-smoke.sh" "$<TARGET_FILE:wled-native>"
  )
  
  add_executable(test_arduino_compat native/tests/test_arduino_compat.cpp)
  target_link_libraries(test_arduino_compat PRIVATE arduino_compat)
  target_compile_features(test_arduino_compat PRIVATE cxx_std_17)
  add_test(
    NAME native_arduino_compat
    COMMAND test_arduino_compat
  )
  
  add_executable(test_fs native/tests/test_fs.cpp)
  target_link_libraries(test_fs PRIVATE wled_fs)
  target_compile_features(test_fs PRIVATE cxx_std_17)
  add_test(
    NAME native_fs
    COMMAND test_fs
  )

  add_executable(test_runtime native/tests/test_runtime.cpp)
  target_link_libraries(test_runtime PRIVATE native_runtime)
  target_compile_features(test_runtime PRIVATE cxx_std_17)
  add_test(
    NAME native_runtime
    COMMAND test_runtime
  )

  add_executable(test_signal_handling native/tests/test_signal_handling.cpp)
  target_link_libraries(test_signal_handling PRIVATE native_runtime)
  target_compile_features(test_signal_handling PRIVATE cxx_std_17)
  add_test(
    NAME native_signal_handling
    COMMAND test_signal_handling
  )

  add_executable(test_network native/tests/test_network.cpp)
  target_link_libraries(test_network PRIVATE native_net)
  target_compile_features(test_network PRIVATE cxx_std_17)
  add_test(
    NAME native_network
    COMMAND test_network
  )
endif()
