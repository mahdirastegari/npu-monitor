// mock_dcmi.hpp — mock DCMI backend for tests and offline validation.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_MOCK_DCMI_HPP
#define NPU_MONITOR_MOCK_DCMI_HPP

#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "npu_monitor/dcmi.hpp"

namespace npu_monitor {

class MockDcmi : public DcmiBackend {
 public:
  MockDcmi() = default;

  void set_utilization_delay_ms(double ms);
  void set_temperature_error(bool fail);
  void set_power_unsupported(bool unsupported);
  void set_process_info_denied(bool denied);
  void inject_fault(int device_index);
  void inject_recovery(int device_index);

  // Call counters for the invariance tests.
  uint64_t multi_utilization_call_count() const {
    return multi_utilization_call_count_.load();
  }
  uint64_t temperature_call_count() const { return temperature_call_count_.load(); }
  uint64_t power_call_count() const { return power_call_count_.load(); }

  bool init() override;
  std::vector<DeviceRef> enumerate() override;
  MultiUtilization multi_utilization(const DeviceRef& dev) override;
  bool temperature(const DeviceRef& dev, double& out) override;
  bool power(const DeviceRef& dev, double& out) override;
  bool memory(const DeviceRef& dev, double& used_mib, double& total_mib) override;
  bool aicore_freq(const DeviceRef& dev, double& out) override;
  bool health(const DeviceRef& dev, unsigned int& out) override;
  bool process_info(const DeviceRef& dev, std::vector<int>& pids,
                    std::vector<unsigned long long>& mem_mib) override;
  std::string driver_version() override;

 private:
  std::mutex mutex_;
  double utilization_delay_ms_ = 0;
  bool temperature_error_ = false;
  bool power_unsupported_ = false;
  bool process_info_denied_ = false;
  std::set<int> faulted_devices_;
  std::atomic<uint64_t> multi_utilization_call_count_{0};
  std::atomic<uint64_t> temperature_call_count_{0};
  std::atomic<uint64_t> power_call_count_{0};
};

}  // namespace npu_monitor

#endif  // NPU_MONITOR_MOCK_DCMI_HPP
