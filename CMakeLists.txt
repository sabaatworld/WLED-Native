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

add_library(native_output
  native/output/RenderBuffer.cpp
)
target_include_directories(native_output PUBLIC native/output)
target_compile_features(native_output PRIVATE cxx_std_17)

add_library(native_core
  native/core/NativeWledCore.cpp
)
target_include_directories(native_core PUBLIC native/core wled00/src/dependencies/json)
target_link_libraries(native_core PUBLIC arduino_compat native_output)
target_compile_features(native_core PRIVATE cxx_std_17)

add_library(native_http
  native/http/NativeHttpServer.cpp
)
target_include_directories(native_http PUBLIC native/http)
target_link_libraries(native_http PUBLIC arduino_compat native_core native_output)
target_compile_features(native_http PRIVATE cxx_std_17)

add_library(native_runtime
  native/runtime/Runtime.cpp
  native/runtime/SignalHandling.cpp
)
target_include_directories(native_runtime PUBLIC native/runtime)
target_link_libraries(native_runtime PUBLIC arduino_compat native_net native_http native_core native_output)
target_compile_features(native_runtime PRIVATE cxx_std_17)
target_compile_definitions(native_runtime PUBLIC WLED_NATIVE_VERSION="${WLED_NATIVE_VERSION}")
target_compile_definitions(native_runtime PRIVATE WLED_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

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

  add_executable(test_http native/tests/test_http.cpp)
  target_link_libraries(test_http PRIVATE native_http native_core native_output)
  target_compile_features(test_http PRIVATE cxx_std_17)
  target_compile_definitions(test_http PRIVATE WLED_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(
    NAME native_http
    COMMAND test_http
  )

  add_executable(test_core native/tests/test_core.cpp)
  target_link_libraries(test_core PRIVATE native_core)
  target_compile_features(test_core PRIVATE cxx_std_17)
  add_test(
    NAME native_core
    COMMAND test_core
  )

  add_executable(test_output native/tests/test_output.cpp)
  target_link_libraries(test_output PRIVATE native_output)
  target_compile_features(test_output PRIVATE cxx_std_17)
  add_test(
    NAME native_output
    COMMAND test_output
  )
endif()
