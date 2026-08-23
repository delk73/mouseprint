#include "query_repository.h"

#include <sqlite3.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const std::string database_path = "/tmp/mouseprint-ui-query-fixture.sqlite3";

void execute(sqlite3* database, const std::string& sql) {
  char* message = nullptr;
  assert(sqlite3_exec(database, sql.c_str(), nullptr, nullptr, &message) == SQLITE_OK);
  sqlite3_free(message);
}

bool fixture_sql(const std::string& path, std::string& contents) {
  std::ifstream input(path);
  if (!input) return false;
  std::ostringstream stream;
  stream << input.rdbuf();
  contents = stream.str();
  return true;
}

sqlite3_int64 scalar(sqlite3* database, const char* sql) {
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const sqlite3_int64 value = sqlite3_column_int64(statement, 0);
  sqlite3_finalize(statement);
  return value;
}

double scalar_double(sqlite3* database, const char* sql) {
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK);
  assert(sqlite3_step(statement) == SQLITE_ROW);
  const double value = sqlite3_column_double(statement, 0);
  sqlite3_finalize(statement);
  return value;
}

bool create_fixture(const std::string& fixture_path) {
  std::filesystem::remove(database_path);
  sqlite3* database = nullptr;
  if (sqlite3_open(database_path.c_str(), &database) != SQLITE_OK) {
    std::cerr << "could not create test database: "
              << (database ? sqlite3_errmsg(database) : "unknown SQLite error") << "\n";
    if (database) sqlite3_close(database);
    return false;
  }
  std::string sql;
  if (!fixture_sql(fixture_path, sql)) {
    std::cerr << "could not open fixture: " << fixture_path << "\n";
    sqlite3_close(database);
    return false;
  }
  execute(database, sql);
  sqlite3_close(database);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " FIXTURE_PATH\n";
    return 2;
  }
  if (!create_fixture(argv[1])) return 2;

  std::string error;
  const auto repository = QueryRepository::open(database_path, error);
  assert(repository);
  assert(error.empty());
  assert(sqlite3_db_readonly(repository->native_handle_for_testing(), "main") == 1);
  assert(scalar(repository->native_handle_for_testing(), "PRAGMA query_only") == 1);
  assert(sqlite3_exec(repository->native_handle_for_testing(),
                      "CREATE TABLE should_not_exist(id INTEGER)", nullptr, nullptr,
                      nullptr) != SQLITE_OK);

  error.clear();
  const auto missing = QueryRepository::open("/tmp/does-not-exist/mouseprint.sqlite3", error);
  assert(!missing);
  assert(!error.empty());

  const auto runs = repository->completed_runs(error);
  assert(error.empty());
  assert(runs.size() == 5);
  assert(runs[0].run_id == 5);
  assert(runs[1].run_id == 4);
  assert(runs[2].run_id == 2);
  assert(runs[3].run_id == 1);
  assert(runs[4].run_id == 0);
  assert(runs[2].ended_wallclock_us && *runs[2].ended_wallclock_us == 9000000);
  assert(runs[2].display_duration_us && *runs[2].display_duration_us == 5000000);
  assert(runs[2].raw_motion_count == 1);
  assert(runs[2].movement_episode_count == 1);
  assert(runs[2].correlation_counts.matched == 1);
  assert(runs[2].correlation_counts.unmatched_context_error == 0);
  assert(runs[2].correlation_counts.unmatched_outside_tolerance == 0);
  assert(runs[2].correlation_counts.unmatched_no_context == 0);
  assert(runs[3].correlation_counts.matched == 4);
  assert(runs[3].correlation_counts.unmatched_context_error == 2);
  assert(runs[3].correlation_counts.unmatched_outside_tolerance == 1);
  assert(runs[3].correlation_counts.unmatched_no_context == 1);

  const auto latest = repository->latest_completed_run(error);
  assert(error.empty());
  assert(latest && latest->run_id == 5);

  const auto sessions = repository->completed_sessions(error);
  assert(error.empty());
  assert(sessions.size() == 5);
  assert(sessions[0].session_id == 5 && sessions[0].run_id == 5);
  assert(sessions[1].session_id == 4 && sessions[1].run_id == 4);
  assert(sessions[2].session_id == 2 && sessions[3].session_id == 1);
  assert(sessions[4].session_id == 0 && sessions[4].run_id == 0);
  assert(sessions[0].ended_wallclock_us && *sessions[0].ended_wallclock_us == 14000000);
  assert(sessions[0].display_duration_us && *sessions[0].display_duration_us == 1000000);
  assert(sessions[0].raw_motion_count == 0);
  assert(sessions[0].movement_episode_count == 0);
  assert(!sessions[0].compositor_path_distance_sum);
  assert(sessions[0].compositor_path_distance_available_count == 0);
  assert(sessions[0].compositor_path_distance_unavailable_count == 0);
  assert(!sessions[0].directional_reversal_total);
  assert(sessions[0].directional_reversal_available_count == 0);
  assert(sessions[0].directional_reversal_unavailable_count == 0);
  assert(sessions[1].raw_motion_count == 1);
  assert(sessions[1].movement_episode_count == 0);
  assert(!sessions[1].compositor_path_distance_sum);
  assert(sessions[1].compositor_path_distance_available_count == 0);
  assert(sessions[1].compositor_path_distance_unavailable_count == 0);
  assert(sessions[2].correlation_counts.matched == 1);
  assert(sessions[3].correlation_counts.matched == 4);
  assert(sessions[3].correlation_counts.unmatched_context_error == 2);
  assert(sessions[3].correlation_counts.unmatched_outside_tolerance == 1);
  assert(sessions[3].correlation_counts.unmatched_no_context == 1);
  assert(sessions[3].compositor_path_distance_sum &&
         *sessions[3].compositor_path_distance_sum == 2.0);
  assert(sessions[3].compositor_path_distance_available_count == 1);
  assert(sessions[3].compositor_path_distance_unavailable_count == 1);
  assert(sessions[3].directional_reversal_total &&
         *sessions[3].directional_reversal_total == 0);
  assert(sessions[3].directional_reversal_available_count == 1);
  assert(sessions[3].directional_reversal_unavailable_count == 1);
  assert(sessions[3].device_metric_status_counts.size() == 2);
  assert(sessions[3].device_metric_status_counts[0].status == "available");
  assert(sessions[3].device_metric_status_counts[0].count == 1);
  assert(sessions[3].device_metric_status_counts[1].status == "missing_unaccelerated_values");
  assert(sessions[3].device_metric_status_counts[1].count == 1);
  assert(sessions[3].compositor_metric_status_counts.size() == 2);
  assert(sessions[3].compositor_metric_status_counts[0].status == "available");
  assert(sessions[3].compositor_metric_status_counts[0].count == 1);
  assert(sessions[3].compositor_metric_status_counts[1].status == "context_sampling_failed");
  assert(sessions[3].compositor_metric_status_counts[1].count == 1);

  const auto latest_session = repository->latest_session(error);
  assert(error.empty());
  assert(latest_session && latest_session->session_id == latest_session->run_id);
  assert(latest_session->session_id == 5);

  const auto device_summaries = repository->device_summaries_for_session(1, error);
  assert(error.empty());
  assert(device_summaries.size() == 2);
  assert(device_summaries[0].device_id == "mouse-a");
  assert(device_summaries[0].device_name && *device_summaries[0].device_name == "Mouse A");
  assert(device_summaries[0].raw_motion_count == 3);
  assert(device_summaries[0].episode_count == 1);
  assert(device_summaries[0].device_path_distance_sum &&
         *device_summaries[0].device_path_distance_sum == 3.0);
  assert(device_summaries[0].device_path_distance_available_count == 1);
  assert(device_summaries[0].device_path_distance_unavailable_count == 0);
  assert(device_summaries[0].compositor_path_distance_sum &&
         *device_summaries[0].compositor_path_distance_sum == 2.0);
  assert(device_summaries[0].compositor_path_distance_available_count == 1);
  assert(device_summaries[0].compositor_path_distance_unavailable_count == 0);
  assert(device_summaries[1].device_id == "mouse-b");
  assert(device_summaries[1].raw_motion_count == 3);
  assert(device_summaries[1].episode_count == 1);
  assert(!device_summaries[1].device_path_distance_sum);
  assert(device_summaries[1].device_path_distance_available_count == 0);
  assert(device_summaries[1].device_path_distance_unavailable_count == 1);
  assert(!device_summaries[1].compositor_path_distance_sum);
  assert(device_summaries[1].compositor_path_distance_available_count == 0);
  assert(device_summaries[1].compositor_path_distance_unavailable_count == 1);

  const auto zero_path_device = repository->device_summaries_for_session(2, error);
  assert(error.empty());
  assert(zero_path_device.size() == 1);
  assert(zero_path_device[0].device_id == "mouse-a");
  assert(zero_path_device[0].device_path_distance_sum &&
         *zero_path_device[0].device_path_distance_sum == 0.0);
  assert(zero_path_device[0].device_path_distance_available_count == 1);
  assert(zero_path_device[0].device_path_distance_unavailable_count == 0);
  assert(zero_path_device[0].compositor_path_distance_sum &&
         *zero_path_device[0].compositor_path_distance_sum == 0.0);
  assert(zero_path_device[0].compositor_path_distance_available_count == 1);
  assert(zero_path_device[0].compositor_path_distance_unavailable_count == 0);
  assert(sessions[2].compositor_path_distance_sum &&
         *sessions[2].compositor_path_distance_sum == 0.0);
  assert(sessions[2].compositor_path_distance_available_count == 1);
  assert(sessions[2].compositor_path_distance_unavailable_count == 0);
  assert(sessions[2].directional_reversal_total &&
         *sessions[2].directional_reversal_total == 0);
  assert(sessions[2].directional_reversal_available_count == 1);
  assert(sessions[2].directional_reversal_unavailable_count == 0);

  assert(sessions[4].movement_episode_count == 1);
  assert(!sessions[4].compositor_path_distance_sum);
  assert(sessions[4].compositor_path_distance_available_count == 0);
  assert(sessions[4].compositor_path_distance_unavailable_count == 1);
  assert(!sessions[4].directional_reversal_total);
  assert(sessions[4].directional_reversal_available_count == 0);
  assert(sessions[4].directional_reversal_unavailable_count == 1);

  const auto raw_only_device = repository->device_summaries_for_session(4, error);
  assert(error.empty());
  assert(raw_only_device.size() == 1);
  assert(raw_only_device[0].device_id == "mouse-c");
  assert(raw_only_device[0].raw_motion_count == 1);
  assert(raw_only_device[0].episode_count == 0);
  assert(!raw_only_device[0].device_path_distance_sum);
  assert(raw_only_device[0].device_path_distance_available_count == 0);
  assert(raw_only_device[0].device_path_distance_unavailable_count == 0);
  assert(!raw_only_device[0].compositor_path_distance_sum);
  assert(raw_only_device[0].compositor_path_distance_available_count == 0);
  assert(raw_only_device[0].compositor_path_distance_unavailable_count == 0);
  assert(repository->device_summaries_for_session(5, error).empty());
  assert(error.empty());
  assert(repository->device_summaries_for_session(3, error).empty());
  assert(error.empty());

  const auto episodes = repository->episodes_for_run(1, error);
  assert(error.empty());
  assert(episodes.size() == 2);
  assert(episodes[0].episode_id == 11);
  assert(episodes[1].episode_id == 12);
  assert(episodes[0].device_id == "mouse-a");
  assert(episodes[0].device_name && *episodes[0].device_name == "Mouse A");
  assert(episodes[0].start_time_us == 100);
  assert(episodes[0].end_time_us == 120);
  assert(episodes[0].duration_us == 20);
  assert(episodes[0].end_reason == "run_end");
  assert(!episodes[0].terminates_in_button_press);
  assert(episodes[0].motion_event_count == 3);
  assert(episodes[0].total_member_count == 3);
  assert(episodes[0].device_path_distance && *episodes[0].device_path_distance == 3.0);
  assert(episodes[0].device_metric_status == "available");
  assert(episodes[0].device_average_velocity && *episodes[0].device_average_velocity == 1.0);
  assert(episodes[0].device_peak_velocity && *episodes[0].device_peak_velocity == 2.0);
  assert(episodes[0].device_directional_reversal_count &&
         *episodes[0].device_directional_reversal_count == 0);
  assert(episodes[0].compositor_path_distance && *episodes[0].compositor_path_distance == 2.0);
  assert(episodes[0].compositor_displacement && *episodes[0].compositor_displacement == 2.0);
  assert(episodes[0].compositor_path_efficiency && *episodes[0].compositor_path_efficiency == 1.0);
  assert(episodes[0].compositor_average_velocity &&
         *episodes[0].compositor_average_velocity == 1.0);
  assert(episodes[0].compositor_peak_velocity && *episodes[0].compositor_peak_velocity == 1.0);
  assert(episodes[0].compositor_metric_status == "available");
  assert(!episodes[1].device_path_distance);
  assert(!episodes[1].device_average_velocity);
  assert(!episodes[1].device_peak_velocity);
  assert(!episodes[1].device_directional_reversal_count);
  assert(episodes[1].device_metric_status == "missing_unaccelerated_values");
  assert(episodes[1].total_member_count == 3);
  assert(episodes[1].compositor_metric_status == "context_sampling_failed");
  assert(!episodes[1].compositor_path_distance);
  assert(!episodes[1].compositor_displacement);
  assert(!episodes[1].compositor_path_efficiency);

  const auto points = repository->trajectory_for_episode(11, error);
  assert(error.empty());
  assert(points.size() == 3);
  assert(points[0].ordinal == 0 && points[1].ordinal == 1 && points[2].ordinal == 2);
  assert(points[0].raw_event_id == 101);
  assert(points[1].raw_event_id == 102);
  assert(points[0].match_id && *points[0].match_id == 701);
  assert(points[0].context_id && *points[0].context_id == 501);
  assert(points[0].match_status && *points[0].match_status == "matched");
  assert(points[0].context_sample_status && *points[0].context_sample_status == "ok");
  assert(points[0].context_id == points[1].context_id);
  assert(points[0].compositor_cumulative_path && *points[0].compositor_cumulative_path == 0.0);
  assert(points[2].context_id && *points[2].context_id == 504);
  assert(points[2].context_sample_status &&
         *points[2].context_sample_status == "cursor_request_failed");
  assert(!points[2].compositor_x);
  assert(!points[2].compositor_cumulative_path);

  const auto gap_points = repository->trajectory_for_episode(12, error);
  assert(error.empty());
  assert(gap_points.size() == 3);
  assert(gap_points[0].raw_event_id == 104);
  assert(gap_points[0].match_id && *gap_points[0].match_id == 704);
  assert(gap_points[0].context_id && *gap_points[0].context_id == 503);
  assert(gap_points[0].match_status && *gap_points[0].match_status == "matched");
  assert(gap_points[0].compositor_x && *gap_points[0].compositor_x == 100.0);
  assert(gap_points[0].compositor_y && *gap_points[0].compositor_y == 200.0);
  assert(gap_points[0].compositor_cumulative_path &&
         *gap_points[0].compositor_cumulative_path == 0.0);
  assert(gap_points[0].device_dx && *gap_points[0].device_dx == 1.0);
  assert(gap_points[1].raw_event_id == 105);
  assert(gap_points[1].match_id && *gap_points[1].match_id == 705);
  assert(gap_points[1].context_id && *gap_points[1].context_id == 505);
  assert(gap_points[1].match_status &&
         *gap_points[1].match_status == "unmatched_context_error");
  assert(gap_points[1].context_sample_status &&
         *gap_points[1].context_sample_status == "cursor_request_failed");
  assert(!gap_points[1].compositor_x);
  assert(!gap_points[1].compositor_y);
  assert(!gap_points[1].compositor_cumulative_path);
  assert(!gap_points[1].device_dx);
  assert(!gap_points[1].device_dy);
  assert(!gap_points[1].device_cumulative_x);
  assert(!gap_points[1].device_cumulative_y);
  assert(!gap_points[1].device_cumulative_path);
  assert(gap_points[2].raw_event_id == 106);
  assert(gap_points[2].match_status && *gap_points[2].match_status == "matched");
  assert(gap_points[2].context_id && *gap_points[2].context_id == 506);
  assert(gap_points[2].compositor_x && *gap_points[2].compositor_x == 110.0);
  assert(gap_points[2].compositor_y && *gap_points[2].compositor_y == 200.0);
  assert(!gap_points[2].compositor_cumulative_path);
  assert(gap_points[2].device_dx && *gap_points[2].device_dx == 2.0);
  assert(gap_points[2].device_dy && *gap_points[2].device_dy == 3.0);
  assert(!gap_points[2].device_cumulative_x);
  assert(!gap_points[2].device_cumulative_y);
  assert(!gap_points[2].device_cumulative_path);

  const sqlite3_int64 raw_before =
      scalar(repository->native_handle_for_testing(), "SELECT count(*) FROM raw_input_events");
  const sqlite3_int64 episodes_before =
      scalar(repository->native_handle_for_testing(), "SELECT count(*) FROM movement_episodes");
  const sqlite3_int64 trajectory_before = scalar(
      repository->native_handle_for_testing(),
      "SELECT count(*) FROM movement_episode_trajectory_points");
  const double compositor_value_before = scalar_double(
      repository->native_handle_for_testing(),
      "SELECT compositor_x FROM movement_episode_trajectory_points "
      "WHERE episode_id=12 AND ordinal=2");
  assert(compositor_value_before == 110.0);
  error.clear();
  assert(repository->completed_runs(error).size() == 5);
  assert(repository->completed_sessions(error).size() == 5);
  assert(repository->device_summaries_for_session(1, error).size() == 2);
  assert(repository->episodes_for_run(1, error).size() == 2);
  assert(repository->trajectory_for_episode(11, error).size() == 3);
  assert(error.empty());
  assert(scalar(repository->native_handle_for_testing(), "SELECT count(*) FROM raw_input_events") ==
         raw_before);
  assert(scalar(repository->native_handle_for_testing(), "SELECT count(*) FROM movement_episodes") ==
         episodes_before);
  assert(scalar(repository->native_handle_for_testing(),
                "SELECT count(*) FROM movement_episode_trajectory_points") == trajectory_before);
  assert(scalar_double(repository->native_handle_for_testing(),
                       "SELECT compositor_x FROM movement_episode_trajectory_points "
                       "WHERE episode_id=12 AND ordinal=2") == compositor_value_before);

  std::cout << "query repository tests passed\n";
}
