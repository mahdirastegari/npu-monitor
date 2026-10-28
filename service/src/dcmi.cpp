// dcmi.cpp — dlopen binding implementation.
//
// Function pointers are resolved once in init() and cached in member
// variables. Missing symbols are recorded as unsupported capabilities
// rather than fatal errors.
//
// License: see LICENSE.
#include "npu_monitor/dcmi.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cstring>

namespace npu_monitor {

namespace {

constexpr const char* kDefaultLibrary =
    "/usr/local/Ascend/driver/lib64/driver/libdcmi.so";

// struct dcmi_multi_utilization_info from dcmi_interface_api.h.
struct MultiUtilizationInfo {
  unsigned int aic_util;
  unsigned int aiv_util;
  unsigned int aicore_util;
  unsigned int npu_util;
  unsigned int reserved[8];
};

// struct dcmi_memory_info from dcmi_interface_api.h (HBM capacity block).
struct HbmInfo {
  unsigned long long memory_size;   // MiB
  unsigned int freq;                // MHz
  unsigned long long memory_usage;  // MiB
  int temp;                         // °C
  unsigned int bandwith_util_rate;  // %
};

// struct dcmi_proc_mem_info from dcmi_interface_api.h.
struct ProcMemInfo {
  int proc_id;
  unsigned long proc_mem_usage;  // bytes
};

constexpr int kMaxProcPerDevice = 64;
constexpr int kDcmiFreqAicoreCurrent = 7;

}  // namespace

struct DcmiDlopen::Impl {
  void* handle = nullptr;
  using InitFn = int (*)(void);
  using CardListFn = int (*)(int*, int*, int);
  using DeviceNumFn = int (*)(int, int*);
  using ChipInfoFn = int (*)(int, int, void*);
  using PcieInfoFn = int (*)(int, int, void*);
  using LogicIdFn = int (*)(int*, int, int);
  using MultiUtilFn = int (*)(int, int, void*);
  using TempFn = int (*)(int, int, int*);
  using PowerFn = int (*)(int, int, int*);
  using HealthFn = int (*)(int, int, unsigned int*);
  using HbmFn = int (*)(int, int, void*);
  using FreqFn = int (*)(int, int, int, unsigned int*);
  using ProcInfoFn = int (*)(int, int, void*, int*);
  using VersionFn = int (*)(char*, unsigned int);

  InitFn init = nullptr;
  CardListFn card_list = nullptr;
  DeviceNumFn dev_num = nullptr;
  ChipInfoFn chip_info_v2 = nullptr;
  ChipInfoFn chip_info_v1 = nullptr;
  PcieInfoFn pcie_info_v2 = nullptr;
  LogicIdFn logic_id = nullptr;
  MultiUtilFn multi_util = nullptr;
  TempFn temperature = nullptr;
  PowerFn power = nullptr;
  HealthFn health = nullptr;
  HbmFn hbm_info = nullptr;
  FreqFn frequency = nullptr;
  ProcInfoFn proc_info = nullptr;
  VersionFn driver_version = nullptr;
};

DcmiDlopen::DcmiDlopen(const std::string& library_path) : impl_(std::make_unique<Impl>()) {
  const char* path = library_path.empty() ? kDefaultLibrary : library_path.c_str();
  impl_->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!impl_->handle) return;
  impl_->init = reinterpret_cast<Impl::InitFn>(dlsym(impl_->handle, "dcmi_init"));
  impl_->card_list = reinterpret_cast<Impl::CardListFn>(dlsym(impl_->handle, "dcmi_get_card_num_list"));
  impl_->dev_num = reinterpret_cast<Impl::DeviceNumFn>(dlsym(impl_->handle, "dcmi_get_device_num_in_card"));
  impl_->chip_info_v2 = reinterpret_cast<Impl::ChipInfoFn>(dlsym(impl_->handle, "dcmi_get_device_chip_info_v2"));
  impl_->chip_info_v1 = reinterpret_cast<Impl::ChipInfoFn>(dlsym(impl_->handle, "dcmi_get_device_chip_info"));
  impl_->pcie_info_v2 = reinterpret_cast<Impl::PcieInfoFn>(dlsym(impl_->handle, "dcmi_get_device_pcie_info_v2"));
  impl_->logic_id = reinterpret_cast<Impl::LogicIdFn>(dlsym(impl_->handle, "dcmi_get_device_logic_id"));
  impl_->multi_util = reinterpret_cast<Impl::MultiUtilFn>(dlsym(impl_->handle, "dcmi_get_device_multi_utilization_rate"));
  impl_->temperature = reinterpret_cast<Impl::TempFn>(dlsym(impl_->handle, "dcmi_get_device_temperature"));
  impl_->power = reinterpret_cast<Impl::PowerFn>(dlsym(impl_->handle, "dcmi_get_device_power_info"));
  impl_->health = reinterpret_cast<Impl::HealthFn>(dlsym(impl_->handle, "dcmi_get_device_health"));
  impl_->hbm_info = reinterpret_cast<Impl::HbmFn>(dlsym(impl_->handle, "dcmi_get_device_hbm_info"));
  impl_->frequency = reinterpret_cast<Impl::FreqFn>(dlsym(impl_->handle, "dcmi_get_device_frequency"));
  impl_->proc_info = reinterpret_cast<Impl::ProcInfoFn>(dlsym(impl_->handle, "dcmi_get_device_resource_info"));
  impl_->driver_version = reinterpret_cast<Impl::VersionFn>(dlsym(impl_->handle, "dcmi_get_driver_version"));
}

DcmiDlopen::~DcmiDlopen() = default;

bool DcmiDlopen::init() {
  if (!impl_->handle || !impl_->init) return false;
  return impl_->init() == 0;
}

std::vector<DeviceRef> DcmiDlopen::enumerate() {
  std::vector<DeviceRef> devices;
  if (!impl_->handle || !impl_->card_list || !impl_->dev_num) return devices;

  int card_num = 0;
  int cards[64] = {0};
  if (impl_->card_list(&card_num, cards, 64) != 0) return devices;
  card_num = std::min(card_num, 64);

  for (int c = 0; c < card_num; c++) {
    int n = 0;
    if (impl_->dev_num(cards[c], &n) != 0) continue;
    n = std::min(n, 64);
    for (int chip = 0; chip < n; chip++) {
      DeviceRef ref;
      ref.card_id = cards[c];
      ref.chip_id = chip;
      ref.physical_id = static_cast<int>(devices.size());

      if (impl_->logic_id) {
        int logical = 0;
        if (impl_->logic_id(&logical, ref.card_id, ref.chip_id) == 0) ref.logical_id = logical;
      }
      if (impl_->chip_info_v2) {
        struct ChipInfoV2 {
          unsigned char chip_type[32];
          unsigned char chip_name[32];
          unsigned char chip_ver[32];
          unsigned int aicore_cnt;
          unsigned char npu_name[32];
        } info{};
        if (impl_->chip_info_v2(ref.card_id, ref.chip_id, &info) == 0) {
          ref.model = reinterpret_cast<const char*>(info.chip_name);
        }
      } else if (impl_->chip_info_v1) {
        struct ChipInfoV1 {
          unsigned char chip_type[32];
          unsigned char chip_name[32];
          unsigned char chip_ver[32];
          unsigned int aicore_cnt;
        } info{};
        if (impl_->chip_info_v1(ref.card_id, ref.chip_id, &info) == 0) {
          ref.model = reinterpret_cast<const char*>(info.chip_name);
        }
      }
      if (impl_->pcie_info_v2) {
        struct PcieInfoAll {
          unsigned int venderid;
          unsigned int subvenderid;
          unsigned int deviceid;
          unsigned int subdeviceid;
          int domain;
          unsigned int bdf_busid;
          unsigned int bdf_deviceid;
          unsigned int bdf_funcid;
          unsigned char reserve[32];
        } info{};
        if (impl_->pcie_info_v2(ref.card_id, ref.chip_id, &info) == 0) {
          char buf[32];
          std::snprintf(buf, sizeof(buf), "%04X:%02X:%02X.%X",
                        info.domain, info.bdf_busid, info.bdf_deviceid, info.bdf_funcid);
          ref.bus_id = buf;
        }
      }
      devices.push_back(ref);
    }
  }
  return devices;
}

MultiUtilization DcmiDlopen::multi_utilization(const DeviceRef& dev) {
  MultiUtilization result;
  if (!impl_->multi_util) return result;
  MultiUtilizationInfo info{};
  if (impl_->multi_util(dev.card_id, dev.chip_id, &info) == 0) {
    result.aic_util = info.aic_util;
    result.aiv_util = info.aiv_util;
    result.aicore_util = info.aicore_util;
    result.npu_util = info.npu_util;
    result.supported = true;
  }
  return result;
}

bool DcmiDlopen::temperature(const DeviceRef& dev, double& out) {
  if (!impl_->temperature) return false;
  int value = 0;
  if (impl_->temperature(dev.card_id, dev.chip_id, &value) != 0) return false;
  out = value;
  return true;
}

bool DcmiDlopen::power(const DeviceRef& dev, double& out) {
  if (!impl_->power) return false;
  int value = 0;
  if (impl_->power(dev.card_id, dev.chip_id, &value) != 0) return false;
  out = value * 0.1;  // DCMI reports 0.1 W; daemon reports watts.
  return true;
}

bool DcmiDlopen::memory(const DeviceRef& dev, double& used_mib, double& total_mib) {
  if (!impl_->hbm_info) return false;
  HbmInfo info{};
  if (impl_->hbm_info(dev.card_id, dev.chip_id, &info) != 0) return false;
  total_mib = static_cast<double>(info.memory_size);
  used_mib = static_cast<double>(info.memory_usage);
  return total_mib > 0;
}

bool DcmiDlopen::aicore_freq(const DeviceRef& dev, double& out) {
  if (!impl_->frequency) return false;
  unsigned int value = 0;
  if (impl_->frequency(dev.card_id, dev.chip_id, kDcmiFreqAicoreCurrent, &value) != 0) return false;
  out = value;
  return true;
}

bool DcmiDlopen::health(const DeviceRef& dev, unsigned int& out) {
  if (!impl_->health) return false;
  unsigned int value = 0;
  if (impl_->health(dev.card_id, dev.chip_id, &value) != 0) return false;
  out = value;
  return true;
}

bool DcmiDlopen::process_info(const DeviceRef& dev, std::vector<int>& pids,
                              std::vector<unsigned long long>& mem_mib) {
  if (!impl_->proc_info) return false;
  std::vector<ProcMemInfo> entries(kMaxProcPerDevice);
  int count = 0;
  if (impl_->proc_info(dev.card_id, dev.chip_id, entries.data(), &count) != 0) return false;
  int n = std::min(count, kMaxProcPerDevice);
  pids.clear();
  mem_mib.clear();
  for (int i = 0; i < n; i++) {
    if (entries[i].proc_id > 0) {
      pids.push_back(entries[i].proc_id);
      mem_mib.push_back(static_cast<unsigned long long>(entries[i].proc_mem_usage));
    }
  }
  return true;
}

std::string DcmiDlopen::driver_version() {
  if (!impl_->driver_version) return "";
  char buf[256] = {0};
  if (impl_->driver_version(buf, sizeof(buf)) != 0) return "";
  return buf;
}

}  // namespace npu_monitor
