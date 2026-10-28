// test_mock.cpp — mock backend with fault injection for deterministic tests.
//
// License: see LICENSE.
#include "npu_monitor/mock_dcmi.hpp"

#include <cmath>
#include <random>

namespace npu_monitor {

void MockDcmi::set_utilization_delay_ms(double ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  utilization_delay_ms_ = ms;
}

void MockDcmi::set_temperature_error(bool fail) {
  std::lock_guard<std::mutex> lock(mutex_);
  temperature_error_ = fail;
}

void MockDcmi::set_power_unsupported(bool unsupported) {
  std::lock_guard<std::mutex> lock(mutex_);
  power_unsupported_ = unsupported;
}

void MockDcmi::set_process_info_denied(bool denied) {
  std::lock_guard<std::mutex> lock(mutex_);
  process_info_denied_ = denied;
}

void MockDcmi::inject_fault(int device_index) {
  std::lock_guard<std::mutex> lock(mutex_);
  faulted_devices_.insert(device_index);
}

void MockDcmi::inject_recovery(int device_index) {
  std::lock_guard<std::mutex> lock(mutex_);
  faulted_devices_.erase(device_index);
}

bool MockDcmi::init() { return true; }

std::vector<DeviceRef> MockDcmi::enumerate() {
  std::vector<DeviceRef> devices;
  for (int i = 0; i < 8; i++) {
    DeviceRef ref;
    ref.card_id = i / 2;
    ref.chip_id = i % 2;
    ref.physical_id = i;
    ref.logical_id = i;
    ref.model = "Ascend910";
    ref.bus_id = "0000:00:00.0";
    devices.push_back(ref);
  }
  return devices;
}

MultiUtilization MockDcmi::multi_utilization(const DeviceRef& dev) {
  multi_utilization_call_count_.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(mutex_);
  if (utilization_delay_ms_ > 0) {
    auto end = std::chrono::steady_clock::now() +
               std::chrono::duration<double, std::milli>(utilization_delay_ms_);
    while (std::chrono::steady_clock::now() < end) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  MultiUtilization mu;
  mu.aic_util = 0;
  mu.aiv_util = 0;
  mu.aicore_util = 0;
  mu.npu_util = 0;
  mu.supported = true;
  return mu;
}

bool MockDcmi::temperature(const DeviceRef& dev, double& out) {
  temperature_call_count_.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(mutex_);
  if (temperature_error_) return false;
  out = 39.0;
  return true;
}

bool MockDcmi::power(const DeviceRef& dev, double& out) {
  power_call_count_.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(mutex_);
  if (power_unsupported_) return false;
  out = 158.7;
  return true;
}

bool MockDcmi::memory(const DeviceRef& dev, double& used_mib, double& total_mib) {
  used_mib = 3137;
  total_mib = 65536;
  return true;
}

bool MockDcmi::aicore_freq(const DeviceRef& dev, double& out) {
  out = 1600;
  return true;
}

bool MockDcmi::health(const DeviceRef& dev, unsigned int& out) {
  out = 0;
  return true;
}

bool MockDcmi::process_info(const DeviceRef& dev, std::vector<int>& pids,
                            std::vector<unsigned long long>& mem_mib) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (process_info_denied_) return false;
  pids.clear();
  mem_mib.clear();
  return true;
}

std::string MockDcmi::driver_version() { return "mock-1.0"; }

}  // namespace npu_monitor
