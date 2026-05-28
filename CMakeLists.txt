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

add_executable(wled-native
  native/main.cpp
)
target_link_libraries(wled-native PRIVATE arduino_compat wled_fs)
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
endif()
