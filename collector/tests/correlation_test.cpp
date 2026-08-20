#define main mouseprint_collector_entry
#include "../src/main.cpp"
#undef main

#include <cassert>
#include <filesystem>
#include <iostream>

namespace {

std::string run_scenario(const std::string& name, const std::vector<ContextSnapshot>& contexts,
                         std::uint64_t input_time_us, const std::string& expected) {
  const std::string path = "/tmp/mouseprint-correlation-" + name + ".sqlite3";
  std::filesystem::remove(path);
  std::filesystem::remove(path + "-wal");
  std::filesystem::remove(path + "-shm");

  Database database;
  assert(database.open(path));
  ContextCorrelator correlator(25000);

  RawEvent event;
  event.device_id = "event4";
  event.event_type = "MOTION";
  event.has_source_time = true;
  event.source_time_us = input_time_us;
  const sqlite3_int64 raw_event_id = database.record_event(event);
  assert(raw_event_id != 0);
  correlator.add_input(database, raw_event_id, input_time_us);
  for (const ContextSnapshot& context : contexts) {
    correlator.add_context(database, context);
  }
  correlator.finish(database);
  database.finish(1);
  database.close();

  sqlite3* verification_db = nullptr;
  assert(sqlite3_open_v2(path.c_str(), &verification_db, SQLITE_OPEN_READONLY, nullptr) ==
         SQLITE_OK);
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(verification_db,
                            "select match_status from input_context_matches", -1, &statement,
                            nullptr) == SQLITE_OK);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const std::string actual = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
  sqlite3_finalize(statement);
  sqlite3_close(verification_db);
  assert(actual == expected);
  return actual;
}

ContextSnapshot valid_context(std::uint64_t time_us) {
  ContextSnapshot context;
  context.sample_monotonic_us = time_us;
  context.request_start_us = time_us;
  context.request_end_us = time_us + 10;
  context.request_latency_us = 10;
  context.sample_status = "ok";
  context.has_cursor = true;
  context.cursor_x = 10;
  context.cursor_y = 20;
  return context;
}

ContextSnapshot failed_context(std::uint64_t time_us) {
  ContextSnapshot context = valid_context(time_us);
  context.sample_status = "cursor_request_failed";
  context.has_cursor = false;
  return context;
}

}  // namespace

int main() {
  assert(run_scenario("matched", {valid_context(100000), valid_context(125000)}, 100000,
                      "matched") == "matched");
  assert(run_scenario("healthy-gap", {valid_context(0), valid_context(130000)}, 100000,
                      "unmatched_outside_tolerance") == "unmatched_outside_tolerance");
  assert(run_scenario("no-context", {}, 100000, "unmatched_no_context") ==
         "unmatched_no_context");
  assert(run_scenario("context-error", {valid_context(0), failed_context(100000)}, 100000,
                      "unmatched_context_error") == "unmatched_context_error");
  std::cout << "correlation classification tests passed\n";
}
