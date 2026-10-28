// dcmi.hpp — dlopen binding to the CANN DCMI device-information API.
//
// The vendor header /usr/local/Ascend/driver/include/dcmi_interface_api.h is
// included directly so struct layouts and enum values come from the installed
// SDK rather than being re-declared here.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_DCMI_HPP
#define NPU_MONITOR_DCMI_HPP

#include <dlfcn.h>

#include <memory>
#include <string>
#include <vector>

#include "npu_monitor/metric.hpp"

namespace npu_monitor {

struct DeviceRef {
  int card_id = 0;    // physical card index (DCMI "card_id")
  int chip_id = 0;   // chip index within the card (DCMI "device_id")
  int physical_id = 0;  // 0-based inventory position
  int logical_id = 0;   // driver-assigned logical index
  std::string model;
  std::string bus_id;   // PCIe BDF, e.g. "0000:9D:00.0"
};

struct MultiUtilization {
  double aic_util = 0;    // AI CPU utilization %
  double aiv_util = 0;    // AI vector utilization %
  double aicore_util = 0; // AI core utilization %
  double npu_util = 0;   // overall NPU utilization %
  bool supported = false;
};

struct DeviceSample {
  MultiUtilization utilization;
  double temperature_c = 0;
  double power_w = 0;
  double memory_used_mib = 0;
  double memory_total_mib = 0;
  double memory_bandwidth_pct = 0;
  double aicore_freq_mhz = 0;
  double aicore_max_freq_mhz = 0;
  double hbm_temp_c = 0;
  unsigned int health = 0;
  int process_count = 0;
  std::vector<int> process_pids;
  std::vector<unsigned long long> process_mem_mib;
};

// A DCMI-like backend. The real implementation dlopens libdcmi.so; the mock
// implementation implements the same interface for tests.
class DcmiBackend {
 public:
  virtual ~DcmiBackend() = default;

  virtual bool init() = 0;
  virtual std::vector<DeviceRef> enumerate() = 0;
  virtual MultiUtilization multi_utilization(const DeviceRef& dev) = 0;
  virtual bool temperature(const DeviceRef& dev, double& out) = 0;
  virtual bool power(const DeviceRef& dev, double& out) = 0;
  virtual bool memory(const DeviceRef& dev, double& used_mib, double& total_mib) = 0;
  virtual bool aicore_freq(const DeviceRef& dev, double& out) = 0;
  virtual bool health(const DeviceRef& dev, unsigned int& out) = 0;
  virtual bool process_info(const DeviceRef& dev, std::vector<int>& pids,
                            std::vector<unsigned long long>& mem_mib) = 0;
  virtual std::string driver_version() = 0;
};

// Production backend: dlopens libdcmi.so and binds the DCMI functions the
// daemon needs. Missing symbols are recorded as "unsupported" capabilities
// rather than fatal errors.
class DcmiDlopen : public DcmiBackend {
 public:
  explicit DcmiDlopen(const std::string& library_path = "");
  ~DcmiDlopen() override;

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
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace npu_monitor

#endif  // NPU_MONITOR_DCMI_HPP
