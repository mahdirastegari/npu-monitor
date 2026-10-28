// proc_enrich.cpp — enrich process entries from /proc.
//
// License: see LICENSE.
#include "npu_monitor/proc_enrich.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace npu_monitor {

std::string proc_user(int pid) {
  // /proc/<pid>/status → "Uid:\t<real>\t<eff>\t<sav>\t<fs>"
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/%d/status", pid);
  std::ifstream f(path);
  if (!f) return "";
  std::string line;
  while (std::getline(f, line)) {
    // "Uid:" is 4 chars — compare exactly those, not 5.
    if (line.compare(0, 4, "Uid:") != 0) continue;
    // Parse the first (real) UID.
    unsigned uid = 0;
    std::istringstream iss(line.substr(5));
    iss >> uid;
    if (uid == 0) return "root";
    // Look up the name in /etc/passwd — uid is the 3rd colon-separated field.
    std::ifstream pwd("/etc/passwd");
    std::string pline;
    while (std::getline(pwd, pline)) {
      // name:password:uid:gid:gecos:home:shell
      std::istringstream ps(pline);
      std::string name, x, uid_str;
      if (std::getline(ps, name, ':') &&
          std::getline(ps, x, ':') &&
          std::getline(ps, uid_str, ':')) {
        unsigned entry_uid = 0;
        std::istringstream us(uid_str);
        us >> entry_uid;
        if (entry_uid == uid) return name;
      }
    }
    return "";  // UID found but no passwd entry
  }
  return "";
}

std::string proc_command(int pid) {
  // /proc/<pid>/cmdline — NUL-separated argv, read as raw bytes.
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
  std::ifstream f(path, std::ios::binary);
  if (f) {
    std::string raw((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    if (!raw.empty()) {
      // Replace NUL separators with spaces; trim trailing NUL.
      for (char& c : raw) {
        if (c == '\0') c = ' ';
      }
      // Trim trailing spaces.
      while (!raw.empty() && raw.back() == ' ') raw.pop_back();
      if (!raw.empty()) return raw;
    }
  }

  // Fall back to /proc/<pid>/comm (truncated to 15 chars by the kernel).
  std::snprintf(path, sizeof(path), "/proc/%d/comm", pid);
  std::ifstream cf(path);
  if (cf) {
    std::string comm;
    std::getline(cf, comm);
    if (!comm.empty()) return comm;
  }

  return "";
}

bool proc_exists(int pid) {
  if (pid <= 0) return false;
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/%d/stat", pid);
  return access(path, F_OK) == 0;
}

}  // namespace npu_monitor