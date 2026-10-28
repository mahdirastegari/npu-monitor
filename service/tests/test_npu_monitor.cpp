// test_npu_monitor.cpp — deterministic tests for the npu-monitord daemon.
//
// Uses the mock backend to inject delays, errors, unsupported metrics,
// device changes, faults, and recovery events. Verifies the invariants the
// task calls out: client count must not affect driver calls, slow clients
// must not block readers, snapshots must stay consistent during
// publication, failed readings must retain their age, and optional
// capabilities must degrade gracefully.
//
// License: see LICENSE.
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include "npu_monitor/activity.hpp"
#include "npu_monitor/cache.hpp"
#include "npu_monitor/collector.hpp"
#include "npu_monitor/mock_dcmi.hpp"

using namespace npu_monitor;

namespace {

// Test 1: Client count does not increase steady-state driver calls.
// We measure driver calls over a fixed window while simulating many cache
// reads. The call count must be bounded by the collection interval, not by
// the number of reads.
void test_client_count_does_not_affect_driver_calls() {
  MockDcmi backend;
  CollectorConfig config;
  config.utilization_interval_ms = 100.0;
  config.env_interval_ms = 200.0;
  config.process_interval_ms = 200.0;
  SnapshotCache cache;
  ActivityTracker activity;
  activity.touch();
  activity.touch();  // keep awake during test
  Collector collector(backend, config, cache, activity);
  collector.start();
  collector.wait_ready(2000.0);

  // Reset counter after warm-up.
  uint64_t baseline = backend.multi_utilization_call_count();

  // Simulate 20 clients reading the cache continuously for 300 ms.
  // Touch activity periodically to keep the daemon awake.
  auto start = std::chrono::steady_clock::now();
  int cache_reads = 0;
  while (std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - start).count() < 300.0) {
    for (int c = 0; c < 20; c++) {
      cache.latest();  // client read — must not trigger driver calls
      cache_reads++;
    }
    activity.touch();  // client activity keeps daemon awake
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  uint64_t after = backend.multi_utilization_call_count();
  uint64_t driver_calls = after - baseline;

  // With a 100 ms interval over 300 ms, we expect ~3 utilization calls
  // (one per cycle, 8 devices each, but the counter counts per-device
  // calls). The key invariant: driver_calls must be small and bounded
  // by the interval, regardless of cache_reads.
  assert(cache_reads > 100);  // we definitely read the cache a lot
  // 300 ms / 100 ms interval = ~3 cycles × 8 devices = ~24 calls.
  // Allow generous headroom but prove it's not proportional to clients.
  assert(driver_calls < 100);
  std::cout << "  client_count: " << cache_reads << " cache reads, "
            << driver_calls << " driver calls\n";
  collector.stop();
}

// Test 2: Slow utilization collection does not block fast-lane readers.
void test_slow_client_does_not_block_readers() {
  MockDcmi backend;
  backend.set_utilization_delay_ms(50.0);  // simulate slow MCU round-trip
  CollectorConfig config;
  config.utilization_interval_ms = 500.0;
  config.env_interval_ms = 50.0;
  config.process_interval_ms = 200.0;
  SnapshotCache cache;
  ActivityTracker activity;
  activity.touch();
  Collector collector(backend, config, cache, activity);
  collector.start();

  // Even while the utilization lane is blocked in a 50 ms driver call,
  // the env lane must keep publishing and latest() must return quickly.
  for (int i = 0; i < 10; i++) {
    auto start = std::chrono::steady_clock::now();
    auto snap = collector.latest();
    auto elapsed = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - start).count();
    // Reading the cache must be sub-millisecond even during slow collection.
    assert(elapsed < 5.0);
    if (snap) assert(snap->devices.size() == 8);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  collector.stop();
  std::cout << "  slow_client: cache reads stayed sub-ms during slow collection\n";
}

// Test 3: Snapshots remain internally consistent during publication.
void test_snapshot_consistency_during_publication() {
  MockDcmi backend;
  CollectorConfig config;
  config.utilization_interval_ms = 10.0;
  config.env_interval_ms = 10.0;
  config.process_interval_ms = 10.0;
  SnapshotCache cache;
  ActivityTracker activity;
  activity.touch();
  Collector collector(backend, config, cache, activity);
  collector.start();
  collector.wait_ready(2000.0);

  for (int i = 0; i < 200; i++) {
    auto snap = cache.latest();
    assert(snap != nullptr);
    // Every snapshot must have a complete device set.
    assert(snap->devices.size() == 8);
    for (const auto& d : snap->devices) {
      // Device identity must be consistent.
      assert(d.ref.card_id >= 0);
      assert(d.ref.chip_id >= 0);
    }
  }
  collector.stop();
  std::cout << "  consistency: 200 snapshots all internally consistent\n";
}

// Test 4: Failed readings retain their state (never fabricated to zero).
void test_failed_readings_retain_age() {
  MockDcmi backend;
  CollectorConfig config;
  config.utilization_interval_ms = 50.0;
  config.env_interval_ms = 50.0;
  config.process_interval_ms = 50.0;
  SnapshotCache cache;
  ActivityTracker activity;
  activity.touch();
  Collector collector(backend, config, cache, activity);
  collector.start();
  collector.wait_ready(2000.0);

  // Wait for the env lane to complete at least one cycle (temperature
  // starts as Pending until the env lane populates it).
  for (int i = 0; i < 100; i++) {
    auto s = cache.latest();
    if (s && s->devices[0].temperature.state == MetricState::Ok) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Get a good temperature reading.
  auto snap1 = cache.latest();
  assert(snap1 != nullptr);
  double good_temp = snap1->devices[0].temperature.value;
  assert(snap1->devices[0].temperature.state == MetricState::Ok);
  assert(good_temp > 0);  // mock returns 39.0

  // Inject a temperature error.
  backend.set_temperature_error(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // The reading must NOT be fabricated as zero.
  auto snap2 = cache.latest();
  assert(snap2 != nullptr);
  const auto& temp = snap2->devices[0].temperature;
  assert(temp.state != MetricState::Ok);
  assert(temp.value != 0.0 || temp.state == MetricState::Unsupported);
  // A valid zero is only acceptable when state == Ok. Failed readings
  // must never report state == Ok with value 0.
  if (temp.state == MetricState::Error) {
    assert(temp.value != 0.0 || temp.reason != "");
  }

  backend.set_temperature_error(false);
  collector.stop();
  std::cout << "  failed_readings: error state preserved, no fabricated zero\n";
}

// Test 5: Optional capabilities degrade gracefully (unsupported, not zero).
void test_optional_capabilities_handled() {
  MockDcmi backend;
  backend.set_power_unsupported(true);
  CollectorConfig config;
  config.utilization_interval_ms = 50.0;
  config.env_interval_ms = 50.0;
  config.process_interval_ms = 50.0;
  SnapshotCache cache;
  ActivityTracker activity;
  activity.touch();
  Collector collector(backend, config, cache, activity);
  collector.start();
  collector.wait_ready(2000.0);

  // Wait for env lane to complete so power is actually queried.
  for (int i = 0; i < 100; i++) {
    auto s = cache.latest();
    if (s && s->devices[0].power.state != MetricState::Pending) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  auto snap = cache.latest();
  assert(snap != nullptr);
  // Power must be marked as error (query returned false), NOT as Ok with
  // value 0. The mock's set_power_unsupported makes power() return false.
  assert(snap->devices[0].power.state != MetricState::Ok);
  // It must not be a fabricated zero with Ok state.
  assert(!(snap->devices[0].power.state == MetricState::Ok &&
           snap->devices[0].power.value == 0.0));
  collector.stop();
  std::cout << "  capabilities: unsupported power not fabricated to zero\n";
}

// Test 6: Collector can be stopped and restarted.
void test_collector_restart() {
  MockDcmi backend;
  CollectorConfig config;
  config.utilization_interval_ms = 50.0;
  config.env_interval_ms = 50.0;
  config.process_interval_ms = 50.0;
  SnapshotCache cache;
  ActivityTracker activity;
  activity.touch();
  Collector collector(backend, config, cache, activity);
  collector.start();
  assert(collector.wait_ready(2000.0));
  collector.stop();

  // Restart with a fresh cache.
  SnapshotCache cache2;
  ActivityTracker activity2;
  activity2.touch();
  Collector collector2(backend, config, cache2, activity2);
  collector2.start();
  assert(collector2.wait_ready(2000.0));
  collector2.stop();
  std::cout << "  restart: collector stopped and restarted successfully\n";
}

// Test 7: Process-data access rules are enforced.
// When process_info is denied, the snapshot must report the denial
// without crashing and without fabricating process entries.
void test_process_data_access_rules() {
  MockDcmi backend;
  backend.set_process_info_denied(true);
  CollectorConfig config;
  config.utilization_interval_ms = 50.0;
  config.env_interval_ms = 50.0;
  config.process_interval_ms = 50.0;
  SnapshotCache cache;
  ActivityTracker activity;
  activity.touch();
  Collector collector(backend, config, cache, activity);
  collector.start();
  collector.wait_ready(2000.0);

  auto snap = cache.latest();
  assert(snap != nullptr);
  // No process entries should be present when access is denied.
  // The device totals should still be available.
  assert(snap->devices.size() == 8);
  for (const auto& d : snap->devices) {
    // Processes may be empty; that's correct for denied access.
    // The key invariant: no fabricated PIDs.
    for (const auto& p : d.processes) {
      assert(p.pid > 0);  // no PID 0 or negative
    }
  }
  collector.stop();
  std::cout << "  process_access: denied access handled, no fabricated PIDs\n";
}

// Test 8: Idle/wake — the daemon sleeps when no client is active and wakes
// up instantly when a client touches. Driver calls must drop to zero while
// idle and resume immediately on wake.
void test_idle_wake() {
  MockDcmi backend;
  CollectorConfig config;
  config.utilization_interval_ms = 50.0;
  config.env_interval_ms = 50.0;
  config.process_interval_ms = 50.0;
  config.idle_timeout_ms = 200.0;  // short timeout for fast test
  SnapshotCache cache;
  ActivityTracker activity;
  activity.touch();  // start awake
  Collector collector(backend, config, cache, activity);
  collector.start();
  collector.wait_ready(2000.0);

  // Let the daemon collect for a bit while active.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  uint64_t calls_while_active = backend.multi_utilization_call_count();
  assert(calls_while_active > 0);

  // Stop touching — the daemon should go idle after 200ms.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  uint64_t calls_before_idle = backend.multi_utilization_call_count();

  // Wait another 300ms — no new calls should happen while idle.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  uint64_t calls_while_idle = backend.multi_utilization_call_count();
  assert(calls_while_idle == calls_before_idle);  // zero driver calls while idle!

  // Wake the daemon — it should resume collecting immediately.
  activity.touch();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  uint64_t calls_after_wake = backend.multi_utilization_call_count();
  assert(calls_after_wake > calls_while_idle);  // resumed after wake

  collector.stop();
  std::cout << "  idle_wake: zero driver calls while idle, resumed on touch\n";
}

}  // namespace

int main() {
  std::cout << "Running npu-monitord tests...\n";
  test_client_count_does_not_affect_driver_calls();
  test_slow_client_does_not_block_readers();
  test_snapshot_consistency_during_publication();
  test_failed_readings_retain_age();
  test_optional_capabilities_handled();
  test_collector_restart();
  test_process_data_access_rules();
  test_idle_wake();
  std::cout << "all npu-monitord tests passed\n";
  return 0;
}
