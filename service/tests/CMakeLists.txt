# Tests for npu-monitord using the mock backend.
add_executable(test_npu_monitor test_npu_monitor.cpp)
target_link_libraries(test_npu_monitor PRIVATE npu_monitor_service)
add_test(NAME npu_monitor_service_tests COMMAND test_npu_monitor)
