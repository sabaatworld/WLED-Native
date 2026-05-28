cmake_minimum_required(VERSION 3.20)

project(WLEDNative LANGUAGES CXX)

set(WLED_NATIVE_VERSION "17.0.0-dev")

add_executable(wled-native
  native/main.cpp
)

target_compile_features(wled-native PRIVATE cxx_std_17)
target_compile_definitions(wled-native PRIVATE WLED_NATIVE_VERSION="${WLED_NATIVE_VERSION}")

include(CTest)
if(BUILD_TESTING)
  add_test(
    NAME native_cli_smoke
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/cli-smoke.sh" "$<TARGET_FILE:wled-native>"
  )
endif()
