# npu-monitor — terminal client for the npu-monitord daemon.
#
# A standalone client: depends only on the socket protocol
# (docs/protocol.md) and FTXUI for the dashboard. Builds and runs
# without any Ascend driver installed.
#
# License: see LICENSE.
include(FetchContent)

# EXCLUDE_FROM_ALL (CMake 3.28+) keeps FTXUI's install rules out of
# `cmake --install` — without it, FTXUI static libs and headers would be
# installed system-wide alongside our binaries.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v7.0.3
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
  )
else()
  FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v7.0.3
    GIT_SHALLOW    TRUE
  )
endif()
FetchContent_MakeAvailable(ftxui)

add_executable(npu-monitor
  src/npu-monitor.cpp
  src/tui.cpp
  src/json_parser.cpp
)
target_include_directories(npu-monitor PRIVATE include)
target_link_libraries(npu-monitor PRIVATE ftxui::ftxui)
target_compile_definitions(npu-monitor PRIVATE NPU_MONITOR_VERSION="${NPU_MONITOR_VERSION}")

install(TARGETS npu-monitor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
