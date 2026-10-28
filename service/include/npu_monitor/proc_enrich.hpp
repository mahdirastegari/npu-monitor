// proc_enrich.hpp — enrich process entries from /proc.
//
// Fills in user and command fields that DCMI doesn't provide.
// All lookups are best-effort: on any failure the fields are left empty.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_PROC_ENRICH_HPP
#define NPU_MONITOR_PROC_ENRICH_HPP

#include <string>

namespace npu_monitor {

// Look up the owning user name for a PID from /proc/<pid>/status (Uid:).
// Returns "" on failure.
std::string proc_user(int pid);

// True if the PID is visible in the host /proc namespace.
// DCMI reports container-namespace PIDs for workloads running inside
// Docker/K8s — those do not exist in the host /proc and cannot be
// resolved to a user from the host.
bool proc_exists(int pid);

// Look up the full command line for a PID from /proc/<pid>/cmdline.
// NUL separators are replaced with spaces. Falls back to /proc/<pid>/comm
// (the truncated basename) if cmdline is empty (kernel threads).
// Returns "" on failure.
std::string proc_command(int pid);

}  // namespace npu_monitor

#endif  // NPU_MONITOR_PROC_ENRICH_HPP
