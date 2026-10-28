# npu-monitord — Ascend NPU telemetry daemon.
#
# Samples the DCMI driver interface once per host and serves cached
# snapshots to any number of clients over a Unix-domain socket.
# See docs/protocol.md for the wire format.
#
# License: see LICENSE.
find_package(Threads REQUIRED)

# --------------------------------------------------------------------------
# Service library
# --------------------------------------------------------------------------
add_library(npu_monitor_service STATIC
  src/dcmi.cpp
  src/snapshot.cpp
  src/collector.cpp
  src/eventbus.cpp
  src/cache.cpp
  src/server.cpp
  src/protocol.cpp
  src/config.cpp
  src/log.cpp
  src/state.cpp
  src/mock_dcmi.cpp
  src/proc_enrich.cpp
)
target_include_directories(npu_monitor_service PUBLIC include)
target_link_libraries(npu_monitor_service PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# --------------------------------------------------------------------------
# Daemon binary (no terminal UI dependency)
# --------------------------------------------------------------------------
add_executable(npu-monitord src/main.cpp)
target_link_libraries(npu-monitord PRIVATE npu_monitor_service)
target_compile_definitions(npu-monitord PRIVATE NPU_MONITORD_VERSION="${NPU_MONITORD_VERSION}")

# --------------------------------------------------------------------------
# Install
# --------------------------------------------------------------------------
install(TARGETS npu-monitord RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES npu-monitor.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/npu_monitor)

# Documentation and license files (NOTICE must accompany distributions).
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/NOTICE
              ${CMAKE_SOURCE_DIR}/README.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/npu-monitor)

# Configure the systemd unit with actual install paths so ExecStart
# matches wherever cmake --install places the binary.
configure_file(npu-monitor.service npu-monitor.service.generated @ONLY)
# Always install to lib/systemd/system (not lib64) — systemd scans both,
# but we want a single predictable location that matches the README.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/npu-monitor.service.generated
        DESTINATION lib/systemd/system
        RENAME npu-monitor.service)

# --------------------------------------------------------------------------
# Tests (mock backend — no NPU hardware required)
# --------------------------------------------------------------------------
if(NPU_MONITOR_BUILD_TESTS)
  add_subdirectory(tests)
endif()
