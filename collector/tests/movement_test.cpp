#define main mouseprint_collector_entry
#include "../src/main.cpp"
#undef main

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

void sql(sqlite3* db, const char* statement) {
  assert(sqlite3_exec(db, statement, nullptr, nullptr, nullptr) == SQLITE_OK);
}

ContextSnapshot context(std::uint64_t time, double x, double y, int monitor = 1,
                        int workspace = 1) {
  ContextSnapshot value;
  value.sample_monotonic_us = time;
  value.request_start_us = time;
  value.request_end_us = time + 1;
  value.request_latency_us = 1;
  value.sample_status = "ok";
  value.has_cursor = true;
  value.cursor_x = x;
  value.cursor_y = y;
  value.monitor_id = monitor;
  value.workspace_id = workspace;
  return value;
}

sqlite3_int64 motion(Database& database, const std::string& device, std::uint64_t time,
                     double dx, double dy, std::optional<ContextSnapshot> snapshot = std::nullopt,
                     const char* match_status = "matched") {
  RawEvent event;
  event.device_id = device;
  event.event_type = "MOTION";
  event.has_source_time = true;
  event.source_time_us = time;
  event.has_unaccelerated = true;
  event.dx_unaccelerated = dx;
  event.dy_unaccelerated = dy;
  const sqlite3_int64 event_id = database.record_event(event);
  if (snapshot) {
    const sqlite3_int64 context_id = database.record_context(*snapshot);
    database.record_match(event_id, context_id, match_status,
                          static_cast<std::int64_t>(snapshot->sample_monotonic_us) -
                              static_cast<std::int64_t>(time), 25000);
  } else {
    database.record_match(event_id, 0, match_status, 0, 25000);
  }
  return event_id;
}

sqlite3_int64 motion_with_context(Database& database, const std::string& device,
                                  std::uint64_t time, double dx, double dy,
                                  sqlite3_int64 context_id, std::uint64_t context_time) {
  RawEvent event;
  event.device_id = device;
  event.event_type = "MOTION";
  event.has_source_time = true;
  event.source_time_us = time;
  event.has_unaccelerated = true;
  event.dx_unaccelerated = dx;
  event.dy_unaccelerated = dy;
  const sqlite3_int64 event_id = database.record_event(event);
  database.record_match(event_id, context_id, "matched",
                        static_cast<std::int64_t>(context_time) -
                            static_cast<std::int64_t>(time), 25000);
  return event_id;
}

void button(Database& database, const std::string& device, std::uint64_t time,
            const char* state) {
  RawEvent event;
  event.device_id = device;
  event.event_type = state == std::string("down") ? "BUTTON_DOWN" : "BUTTON_UP";
  event.has_source_time = true;
  event.source_time_us = time;
  event.has_button = true;
  event.button = 1;
  event.button_state = state;
  const sqlite3_int64 event_id = database.record_event(event);
  database.record_match(event_id, 0, "unmatched_no_context", 0, 25000);
}

void scroll(Database& database, const std::string& device, std::uint64_t time) {
  RawEvent event;
  event.device_id = device;
  event.event_type = "SCROLL";
  event.has_source_time = true;
  event.source_time_us = time;
  event.has_scroll = true;
  event.scroll_source = "wheel";
  const sqlite3_int64 event_id = database.record_event(event);
  database.record_match(event_id, 0, "unmatched_no_context", 0, 25000);
}

std::string status(sqlite3* db, const std::string& device) {
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT compositor_metric_status FROM movement_episodes "
                                "WHERE device_id=? ORDER BY start_time_us", -1, &statement,
                            nullptr) == SQLITE_OK);
  sqlite3_bind_text(statement, 1, device.c_str(), -1, SQLITE_TRANSIENT);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const std::string result = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
  sqlite3_finalize(statement);
  return result;
}

double value(sqlite3* db, const char* column, const std::string& device) {
  const std::string query = "SELECT " + std::string(column) +
                            " FROM movement_episodes WHERE device_id=? ORDER BY start_time_us";
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(db, query.c_str(), -1, &statement, nullptr) == SQLITE_OK);
  sqlite3_bind_text(statement, 1, device.c_str(), -1, SQLITE_TRANSIENT);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const double result = sqlite3_column_double(statement, 0);
  sqlite3_finalize(statement);
  return result;
}

int count_members(sqlite3* db, const std::string& device) {
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT count(*) FROM movement_episode_members m "
                                "JOIN movement_episodes e ON e.episode_id=m.episode_id "
                                "WHERE e.device_id=? AND m.member_role='motion'", -1,
                            &statement, nullptr) == SQLITE_OK);
  sqlite3_bind_text(statement, 1, device.c_str(), -1, SQLITE_TRANSIENT);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const int result = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return result;
}

int total_members(sqlite3* db, const std::string& device) {
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT count(*) FROM movement_episode_members m "
                                "JOIN movement_episodes e ON e.episode_id=m.episode_id "
                                "WHERE e.device_id=?", -1, &statement, nullptr) == SQLITE_OK);
  sqlite3_bind_text(statement, 1, device.c_str(), -1, SQLITE_TRANSIENT);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const int result = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return result;
}

int episode_count(sqlite3* db, const std::string& device) {
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT count(*) FROM movement_episodes WHERE device_id=?", -1,
                            &statement, nullptr) == SQLITE_OK);
  sqlite3_bind_text(statement, 1, device.c_str(), -1, SQLITE_TRANSIENT);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const int result = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return result;
}

int integer_value(sqlite3* db, const char* column, const std::string& device) {
  const std::string query = "SELECT " + std::string(column) +
                            " FROM movement_episodes WHERE device_id=? ORDER BY start_time_us";
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(db, query.c_str(), -1, &statement, nullptr) == SQLITE_OK);
  sqlite3_bind_text(statement, 1, device.c_str(), -1, SQLITE_TRANSIENT);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const int result = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return result;
}

struct TrajectoryPoint {
  int ordinal = 0;
  sqlite3_int64 raw_event_id = 0;
  std::optional<sqlite3_int64> match_id;
  std::uint64_t source_time_us = 0;
  std::optional<double> device_dx, device_dy, device_x, device_y, device_path;
  std::optional<sqlite3_int64> context_id;
  std::optional<std::uint64_t> context_time_us;
  std::optional<double> compositor_x, compositor_y, compositor_path;
};

TrajectoryPoint trajectory(sqlite3* db, const std::string& device, int offset) {
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(
             db,
             "SELECT p.ordinal,p.raw_event_id,p.match_id,p.source_time_us,p.device_dx,"
             "p.device_dy,p.device_cumulative_x,p.device_cumulative_y,"
             "p.device_cumulative_path,p.context_id,p.context_sample_time_us,"
             "p.compositor_x,p.compositor_y,p.compositor_cumulative_path "
             "FROM movement_episode_trajectory_points p "
             "JOIN movement_episodes e ON e.episode_id=p.episode_id "
             "WHERE e.device_id=? ORDER BY e.start_time_us,p.ordinal LIMIT 1 OFFSET ?",
             -1, &statement, nullptr) == SQLITE_OK);
  sqlite3_bind_text(statement, 1, device.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(statement, 2, offset);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  TrajectoryPoint point;
  point.ordinal = sqlite3_column_int(statement, 0);
  point.raw_event_id = sqlite3_column_int64(statement, 1);
  if (sqlite3_column_type(statement, 2) != SQLITE_NULL) {
    point.match_id = sqlite3_column_int64(statement, 2);
  }
  point.source_time_us = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 3));
  const auto optional_double = [&](int column) -> std::optional<double> {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_double(statement, column);
  };
  point.device_dx = optional_double(4);
  point.device_dy = optional_double(5);
  point.device_x = optional_double(6);
  point.device_y = optional_double(7);
  point.device_path = optional_double(8);
  if (sqlite3_column_type(statement, 9) != SQLITE_NULL) {
    point.context_id = sqlite3_column_int64(statement, 9);
  }
  if (sqlite3_column_type(statement, 10) != SQLITE_NULL) {
    point.context_time_us = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 10));
  }
  point.compositor_x = optional_double(11);
  point.compositor_y = optional_double(12);
  point.compositor_path = optional_double(13);
  sqlite3_finalize(statement);
  return point;
}

int trajectory_count(sqlite3* db, const std::string& device) {
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(
             db,
             "SELECT count(*) FROM movement_episode_trajectory_points p "
             "JOIN movement_episodes e ON e.episode_id=p.episode_id WHERE e.device_id=?",
             -1, &statement, nullptr) == SQLITE_OK);
  sqlite3_bind_text(statement, 1, device.c_str(), -1, SQLITE_TRANSIENT);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const int result = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return result;
}

int total_trajectory_count(sqlite3* db) {
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT count(*) FROM movement_episode_trajectory_points", -1,
                            &statement, nullptr) == SQLITE_OK);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const int result = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return result;
}

}  // namespace

int main() {
  const std::string path = "/tmp/mouseprint-movement.sqlite3";
  std::filesystem::remove(path);
  std::filesystem::remove(path + "-wal");
  std::filesystem::remove(path + "-shm");
  Database database;
  assert(database.open(path));
  sql(database.handle(), "INSERT INTO devices(device_id,device_name) VALUES "
                         "('straight','straight'),('curve','curve'),('split','split'),"
                         "('click','click'),('scroll','scroll'),('unmatched','unmatched'),"
                         "('failed','failed'),('zero','zero'),('monitor','monitor'),"
                         "('workspace','workspace'),('reverse','reverse'),('missing','missing'),"
                         "('repeat','repeat'),('timing','timing'),('other','other'),"
                         "('second','second'),('gap','gap')");

  const auto straight_1 = motion(database, "straight", 100000, 1, 0, context(100000, 0, 0));
  const auto straight_2 = motion(database, "straight", 110000, 1, 0, context(110000, 1, 0));
  const auto straight_3 = motion(database, "straight", 120000, 1, 0, context(120000, 2, 0));

  motion(database, "curve", 300000, 3, 4, context(300000, 0, 0));
  motion(database, "curve", 310000, 0, 0, context(310000, 3, 4));
  motion(database, "curve", 320000, -3, -4, context(320000, 3, 0));

  motion(database, "split", 500000, 1, 0, context(500000, 0, 0));
  motion(database, "split", 650001, 1, 0, context(650001, 1, 0));

  motion(database, "click", 800000, 1, 0, context(800000, 0, 0));
  button(database, "click", 810000, "down");
  button(database, "click", 820000, "up");

  motion(database, "scroll", 900000, 1, 0, context(900000, 0, 0));
  scroll(database, "scroll", 905000);
  motion(database, "scroll", 910000, 1, 0, context(910000, 1, 0));

  const auto unmatched_event =
      motion(database, "unmatched", 1000000, 1, 0, std::nullopt, "unmatched_outside_tolerance");
  motion(database, "failed", 1100000, 1, 0, std::nullopt, "unmatched_context_error");
  motion(database, "zero", 1200000, 1, 0, context(1200000, 5, 5));
  motion(database, "zero", 1210000, 1, 0, context(1210000, 5, 5));
  motion(database, "monitor", 1300000, 1, 0, context(1300000, 0, 0, 1, 1));
  motion(database, "monitor", 1310000, 1, 0, context(1310000, 1, 0, 2, 1));
  motion(database, "workspace", 1400000, 1, 0, context(1400000, 0, 0, 1, 1));
  motion(database, "workspace", 1410000, 1, 0, context(1410000, 1, 0, 1, 2));
  motion(database, "reverse", 1500000, 1, 0, context(1500000, 0, 0));
  motion(database, "reverse", 1505000, 0, 0, context(1505000, 0, 0));
  motion(database, "reverse", 1510000, -1, 0, context(1510000, 1, 0));
  motion(database, "missing", 1600000, 1, 0, context(1600000, 0, 0));
  RawEvent missing;
  missing.device_id = "missing";
  missing.event_type = "MOTION";
  missing.has_source_time = true;
  missing.source_time_us = 1610000;
  const auto missing_id = database.record_event(missing);
  const auto missing_context = database.record_context(context(1610000, 1, 0));
  database.record_match(missing_id, missing_context, "matched", 0, 25000);
  motion(database, "missing", 1620000, 2, 3, context(1620000, 2, 0));

  const auto repeat_context_a = database.record_context(context(1700000, 0, 0));
  const auto repeat_context_b = database.record_context(context(1720000, 1, 0));
  motion_with_context(database, "repeat", 1700000, 1, 0, repeat_context_a, 1700000);
  motion_with_context(database, "repeat", 1710000, 1, 0, repeat_context_a, 1700000);
  motion_with_context(database, "repeat", 1720000, 1, 0, repeat_context_b, 1720000);
  const auto gap_context_a = database.record_context(context(2100000, 0, 0));
  const auto gap_context_b = database.record_context(context(2120000, 10, 0));
  motion_with_context(database, "gap", 2100000, 1, 0, gap_context_a, 2100000);
  motion(database, "gap", 2110000, 1, 0, std::nullopt, "unmatched_outside_tolerance");
  motion_with_context(database, "gap", 2120000, 1, 0, gap_context_b, 2120000);
  motion(database, "timing", 1800000, 1, 0, context(1800000, 0, 0));
  motion(database, "timing", 1801000, 1, 0, context(1810000, 1, 0));
  motion(database, "other", 1900000, 1, 0, context(1900000, 0, 0));
  motion(database, "second", 1950000, 1, 0, context(1950000, 0, 0));
  motion(database, "other", 2000000, 1, 0, context(2000000, 1, 0));

  database.finish(0);
  assert(derive_movement_episodes(database.handle()));
  assert(status(database.handle(), "unmatched") == "unmatched_context");
  assert(status(database.handle(), "failed") == "context_sampling_failed");
  assert(status(database.handle(), "monitor") == "monitor_transition");
  assert(status(database.handle(), "workspace") == "workspace_transition");
  assert(status(database.handle(), "other") == "other_device_motion");
  assert(episode_count(database.handle(), "split") == 2);
  assert(episode_count(database.handle(), "click") == 1);
  assert(integer_value(database.handle(), "terminates_in_button_press", "click") == 1);
  assert(episode_count(database.handle(), "scroll") == 1);
  assert(count_members(database.handle(), "scroll") == 2);
  assert(total_members(database.handle(), "click") == 2);
  assert(episode_count(database.handle(), "repeat") == 1);
  assert(episode_count(database.handle(), "reverse") == 1);
  assert(integer_value(database.handle(), "device_directional_reversal_count", "reverse") == 1);
  assert(integer_value(database.handle(), "device_directional_reversal_count", "other") == 0);
  assert(status(database.handle(), "other") == "other_device_motion");
  assert(std::abs(value(database.handle(), "device_path_distance", "straight") - 3.0) < 1e-9);
  assert(std::abs(value(database.handle(), "compositor_path_efficiency", "curve") - (1.0 / 3.0)) <
         1e-9);
  assert(std::abs(value(database.handle(), "compositor_peak_velocity", "timing") - 100.0) < 1e-9);
  assert(std::abs(value(database.handle(), "compositor_path_distance", "repeat") - 1.0) < 1e-9);
  assert(std::abs(value(database.handle(), "compositor_peak_velocity", "repeat") - 50.0) < 1e-9);
  assert(std::abs(value(database.handle(), "device_path_distance", "other") - 2.0) < 1e-9);

  assert(trajectory_count(database.handle(), "straight") == 3);
  const TrajectoryPoint straight_point_1 = trajectory(database.handle(), "straight", 0);
  const TrajectoryPoint straight_point_2 = trajectory(database.handle(), "straight", 1);
  const TrajectoryPoint straight_point_3 = trajectory(database.handle(), "straight", 2);
  assert(straight_point_1.ordinal == 0 && straight_point_1.raw_event_id == straight_1);
  assert(straight_point_2.ordinal == 1 && straight_point_2.raw_event_id == straight_2);
  assert(straight_point_3.ordinal == 2 && straight_point_3.raw_event_id == straight_3);
  assert(std::abs(*straight_point_1.device_x - 1.0) < 1e-9);
  assert(std::abs(*straight_point_2.device_x - 2.0) < 1e-9);
  assert(std::abs(*straight_point_3.device_x - 3.0) < 1e-9);
  assert(std::abs(*straight_point_3.device_path - 3.0) < 1e-9);
  assert(std::abs(*straight_point_1.compositor_x - 0.0) < 1e-9);
  assert(std::abs(*straight_point_2.compositor_x - 1.0) < 1e-9);
  assert(std::abs(*straight_point_3.compositor_x - 2.0) < 1e-9);
  assert(*straight_point_3.context_time_us == 120000);

  assert(trajectory_count(database.handle(), "curve") == 3);
  const TrajectoryPoint curve_point_2 = trajectory(database.handle(), "curve", 1);
  const TrajectoryPoint curve_point_3 = trajectory(database.handle(), "curve", 2);
  assert(std::abs(*curve_point_2.device_x - 3.0) < 1e-9);
  assert(std::abs(*curve_point_2.device_y - 4.0) < 1e-9);
  assert(std::abs(*curve_point_2.device_path - 5.0) < 1e-9);
  assert(std::abs(*curve_point_3.device_x) < 1e-9);
  assert(std::abs(*curve_point_3.device_y) < 1e-9);
  assert(std::abs(*curve_point_3.device_path - 10.0) < 1e-9);

  const TrajectoryPoint reverse_point_2 = trajectory(database.handle(), "reverse", 1);
  assert(std::abs(*reverse_point_2.device_dx) < 1e-9);
  assert(std::abs(*reverse_point_2.device_x - 1.0) < 1e-9);
  assert(std::abs(*reverse_point_2.device_path - 1.0) < 1e-9);
  assert(integer_value(database.handle(), "device_directional_reversal_count", "reverse") == 1);

  const TrajectoryPoint missing_point_1 = trajectory(database.handle(), "missing", 0);
  const TrajectoryPoint missing_point_2 = trajectory(database.handle(), "missing", 1);
  const TrajectoryPoint missing_point_3 = trajectory(database.handle(), "missing", 2);
  assert(std::abs(*missing_point_1.device_x - 1.0) < 1e-9);
  assert(std::abs(*missing_point_1.device_y) < 1e-9);
  assert(std::abs(*missing_point_1.device_path - 1.0) < 1e-9);
  assert(!missing_point_2.device_dx);
  assert(!missing_point_2.device_dy);
  assert(!missing_point_2.device_x);
  assert(!missing_point_2.device_y);
  assert(!missing_point_2.device_path);
  assert(std::abs(*missing_point_3.device_dx - 2.0) < 1e-9);
  assert(std::abs(*missing_point_3.device_dy - 3.0) < 1e-9);
  assert(!missing_point_3.device_x);
  assert(!missing_point_3.device_y);
  assert(!missing_point_3.device_path);

  const TrajectoryPoint repeat_point_1 = trajectory(database.handle(), "repeat", 0);
  const TrajectoryPoint repeat_point_2 = trajectory(database.handle(), "repeat", 1);
  const TrajectoryPoint repeat_point_3 = trajectory(database.handle(), "repeat", 2);
  assert(repeat_point_1.context_id == repeat_point_2.context_id);
  assert(std::abs(*repeat_point_1.compositor_path) < 1e-9);
  assert(std::abs(*repeat_point_2.compositor_path) < 1e-9);
  assert(std::abs(*repeat_point_3.compositor_path - 1.0) < 1e-9);
  assert(std::abs(*repeat_point_3.compositor_x - 1.0) < 1e-9);

  const TrajectoryPoint gap_point_1 = trajectory(database.handle(), "gap", 0);
  const TrajectoryPoint gap_point_2 = trajectory(database.handle(), "gap", 1);
  const TrajectoryPoint gap_point_3 = trajectory(database.handle(), "gap", 2);
  assert(std::abs(*gap_point_1.compositor_x) < 1e-9);
  assert(std::abs(*gap_point_1.compositor_path) < 1e-9);
  assert(!gap_point_2.compositor_x);
  assert(!gap_point_2.compositor_path);
  assert(std::abs(*gap_point_3.compositor_x - 10.0) < 1e-9);
  assert(gap_point_3.context_id == gap_context_b);
  assert(!gap_point_3.compositor_path);

  const TrajectoryPoint unmatched_point = trajectory(database.handle(), "unmatched", 0);
  assert(unmatched_point.raw_event_id == unmatched_event);
  assert(unmatched_point.match_id);
  assert(!unmatched_point.context_id);
  assert(!unmatched_point.compositor_x);
  assert(!unmatched_point.compositor_y);
  assert(!unmatched_point.compositor_path);

  const int original_trajectory_count = total_trajectory_count(database.handle());
  assert(derive_movement_episodes(database.handle()));
  assert(total_trajectory_count(database.handle()) == original_trajectory_count);
  assert(trajectory_count(database.handle(), "straight") == 3);
  std::cout << "movement episode tests passed\n";
}
