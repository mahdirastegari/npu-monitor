# npu-monitor — Ascend NPU telemetry: daemon + terminal client.
#
#   service/  npu-monitord — the telemetry daemon
#   client/   npu-monitor  — the terminal client
#
# Build:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#
# License: see LICENSE.
cmake_minimum_required(VERSION 3.16)
project(npu_monitor LANGUAGES CXX)

# Component versions. Bump the service version on any wire-protocol change
# (docs/protocol.md); the client and service versions track independently.
set(NPU_MONITORD_VERSION "1.0.0" CACHE STRING "npu-monitord (service) version")
set(NPU_MONITOR_VERSION  "1.0.0" CACHE STRING "npu-monitor (client) version")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

include(GNUInstallDirs)

option(NPU_MONITOR_BUILD_TESTS "Build tests" ON)
if(NPU_MONITOR_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(service)
add_subdirectory(client)
