cmake_minimum_required(VERSION 3.20)

project(WLEDNative LANGUAGES CXX)

file(READ "${CMAKE_SOURCE_DIR}/package.json" WLED_PACKAGE_JSON)
string(REGEX MATCH "\"version\"[ \t]*:[ \t]*\"([^\"]+)\"" _ "${WLED_PACKAGE_JSON}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Unable to extract version from package.json")
endif()

set(WLED_VERSION_STRING "${CMAKE_MATCH_1}")

add_executable(wled-native
  wled00/wled_host_cli.cpp
  wled00/wled_host_main.cpp
)

target_compile_features(wled-native PRIVATE cxx_std_17)
target_compile_definitions(wled-native PRIVATE
  WLED_HOST_PRODUCT_NAME="WLED"
  WLED_HOST_VERSION="${WLED_VERSION_STRING}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(wled-native PRIVATE -Wall -Wextra -Wpedantic)
endif()
