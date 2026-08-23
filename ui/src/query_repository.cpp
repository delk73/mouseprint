#include "query_repository.h"

#include <sqlite3.h>

#include <utility>

namespace {

std::string sqlite_error(sqlite3* database, const char* prefix) {
  return std::string(prefix) + ": " + (database ? sqlite3_errmsg(database) : "no database");
}

bool prepare(sqlite3* database, const char* sql, sqlite3_stmt*& statement,
             std::string& error) {
  if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
    error = sqlite_error(database, "could not prepare query");
    return false;
  }
  return true;
}

bool bind_id(sqlite3_stmt* statement, int index, std::int64_t value, std::string& error,
             sqlite3* database) {
  if (sqlite3_bind_int64(statement, index, static_cast<sqlite3_int64>(value)) != SQLITE_OK) {
    error = sqlite_error(database, "could not bind query parameter");
    return false;
  }
  return true;
}

template <typename Value>
std::optional<Value> optional_value(sqlite3_stmt* statement, int column);

template <>
std::optional<std::int64_t> optional_value<std::int64_t>(sqlite3_stmt* statement, int column) {
  if (sqlite3_column_type(statement, column) == SQLITE_NULL) return std::nullopt;
  return static_cast<std::int64_t>(sqlite3_column_int64(statement, column));
}

template <>
std::optional<double> optional_value<double>(sqlite3_stmt* statement, int column) {
  if (sqlite3_column_type(statement, column) == SQLITE_NULL) return std::nullopt;
  return sqlite3_column_double(statement, column);
}

template <>
std::optional<std::string> optional_value<std::string>(sqlite3_stmt* statement, int column) {
  if (sqlite3_column_type(statement, column) == SQLITE_NULL) return std::nullopt;
  const auto* text = sqlite3_column_text(statement, column);
  return std::string(reinterpret_cast<const char*>(text ? text : reinterpret_cast<const unsigned char*>("")));
}

std::string required_text(sqlite3_stmt* statement, int column) {
  const auto* text = sqlite3_column_text(statement, column);
  return text ? reinterpret_cast<const char*>(text) : std::string();
}

}  // namespace

QueryRepository::~QueryRepository() {
  if (database_) sqlite3_close(database_);
}

std::unique_ptr<QueryRepository> QueryRepository::open(const std::string& path,
                                                       std::string& error) {
  sqlite3* database = nullptr;
  if (sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    error = sqlite_error(database, "could not open database read-only");
    if (database) sqlite3_close(database);
    return nullptr;
  }
  if (sqlite3_exec(database, "PRAGMA query_only=ON", nullptr, nullptr, nullptr) != SQLITE_OK) {
    error = sqlite_error(database, "could not enable SQLite query_only mode");
    sqlite3_close(database);
    return nullptr;
  }
  return std::unique_ptr<QueryRepository>(new QueryRepository(database));
}

std::vector<CompletedRunSummary> QueryRepository::completed_runs(std::string& error) const {
  error.clear();
  std::vector<CompletedRunSummary> result;
  const char* sql =
      "SELECT r.run_id, r.started_wallclock_us, r.ended_wallclock_us, "
      "(SELECT count(*) FROM raw_input_events e WHERE e.run_id=r.run_id AND e.event_type='MOTION'), "
      "(SELECT count(*) FROM movement_episodes e WHERE e.run_id=r.run_id), "
      "(SELECT count(*) FROM input_context_matches m WHERE m.run_id=r.run_id AND m.match_status='matched'), "
      "(SELECT count(*) FROM input_context_matches m WHERE m.run_id=r.run_id AND m.match_status='unmatched_context_error'), "
      "(SELECT count(*) FROM input_context_matches m WHERE m.run_id=r.run_id AND m.match_status='unmatched_outside_tolerance'), "
      "(SELECT count(*) FROM input_context_matches m WHERE m.run_id=r.run_id AND m.match_status='unmatched_no_context') "
      "FROM collector_runs r WHERE r.ended_wallclock_us IS NOT NULL "
      "ORDER BY r.started_wallclock_us DESC, r.run_id DESC";
  sqlite3_stmt* statement = nullptr;
  if (!prepare(database_, sql, statement, error)) return result;
  int status = SQLITE_OK;
  while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
    CompletedRunSummary run;
    run.run_id = sqlite3_column_int64(statement, 0);
    run.started_wallclock_us = sqlite3_column_int64(statement, 1);
    run.ended_wallclock_us = optional_value<std::int64_t>(statement, 2);
    if (run.ended_wallclock_us) {
      run.display_duration_us = *run.ended_wallclock_us - run.started_wallclock_us;
    }
    run.raw_motion_count = sqlite3_column_int64(statement, 3);
    run.movement_episode_count = sqlite3_column_int64(statement, 4);
    run.correlation_counts.matched = sqlite3_column_int64(statement, 5);
    run.correlation_counts.unmatched_context_error = sqlite3_column_int64(statement, 6);
    run.correlation_counts.unmatched_outside_tolerance = sqlite3_column_int64(statement, 7);
    run.correlation_counts.unmatched_no_context = sqlite3_column_int64(statement, 8);
    result.push_back(std::move(run));
  }
  if (status != SQLITE_DONE) error = sqlite_error(database_, "could not read completed runs");
  sqlite3_finalize(statement);
  return result;
}

std::vector<SessionSummary> QueryRepository::completed_sessions(std::string& error) const {
  error.clear();
  const auto runs = completed_runs(error);
  std::vector<SessionSummary> result;
  if (!error.empty()) return result;

  const char* sql =
      "SELECT device_metric_status, count(*) FROM movement_episodes "
      "WHERE run_id=? AND device_metric_status IS NOT NULL "
      "GROUP BY device_metric_status ORDER BY device_metric_status";
  const char* compositor_sql =
      "SELECT compositor_metric_status, count(*) FROM movement_episodes "
      "WHERE run_id=? AND compositor_metric_status IS NOT NULL "
      "GROUP BY compositor_metric_status ORDER BY compositor_metric_status";
  const char* aggregate_sql =
      "SELECT sum(compositor_path_distance), count(compositor_path_distance), "
      "count(*) - count(compositor_path_distance), sum(device_directional_reversal_count), "
      "count(device_directional_reversal_count), "
      "count(*) - count(device_directional_reversal_count) "
      "FROM movement_episodes WHERE run_id=?";

  auto status_counts = [&](std::int64_t run_id, const char* query,
                           std::vector<StatusCount>& counts) -> bool {
    sqlite3_stmt* statement = nullptr;
    if (!prepare(database_, query, statement, error) ||
        !bind_id(statement, 1, run_id, error, database_)) {
      sqlite3_finalize(statement);
      return false;
    }
    int status = SQLITE_OK;
    while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
      counts.push_back({required_text(statement, 0), sqlite3_column_int64(statement, 1)});
    }
    if (status != SQLITE_DONE) error = sqlite_error(database_, "could not read session status counts");
    sqlite3_finalize(statement);
    return error.empty();
  };

  result.reserve(runs.size());
  for (const auto& run : runs) {
    SessionSummary session;
    session.session_id = run.run_id;
    session.run_id = run.run_id;
    session.started_wallclock_us = run.started_wallclock_us;
    session.ended_wallclock_us = run.ended_wallclock_us;
    session.display_duration_us = run.display_duration_us;
    session.raw_motion_count = run.raw_motion_count;
    session.movement_episode_count = run.movement_episode_count;
    session.correlation_counts = run.correlation_counts;
    if (!status_counts(run.run_id, sql, session.device_metric_status_counts) ||
        !status_counts(run.run_id, compositor_sql, session.compositor_metric_status_counts)) {
      return {};
    }
    sqlite3_stmt* statement = nullptr;
    if (!prepare(database_, aggregate_sql, statement, error) ||
        !bind_id(statement, 1, run.run_id, error, database_)) {
      sqlite3_finalize(statement);
      return {};
    }
    if (sqlite3_step(statement) != SQLITE_ROW) {
      error = sqlite_error(database_, "could not read session aggregates");
      sqlite3_finalize(statement);
      return {};
    }
    session.compositor_path_distance_sum = optional_value<double>(statement, 0);
    session.compositor_path_distance_available_count = sqlite3_column_int64(statement, 1);
    session.compositor_path_distance_unavailable_count = sqlite3_column_int64(statement, 2);
    session.directional_reversal_total = optional_value<std::int64_t>(statement, 3);
    session.directional_reversal_available_count = sqlite3_column_int64(statement, 4);
    session.directional_reversal_unavailable_count = sqlite3_column_int64(statement, 5);
    sqlite3_finalize(statement);
    result.push_back(std::move(session));
  }
  return result;
}

std::optional<SessionSummary> QueryRepository::latest_session(std::string& error) const {
  const auto sessions = completed_sessions(error);
  if (!error.empty() || sessions.empty()) return std::nullopt;
  return sessions.front();
}

std::vector<DeviceSessionSummary> QueryRepository::device_summaries_for_session(
    std::int64_t session_id, std::string& error) const {
  error.clear();
  std::vector<DeviceSessionSummary> result;
  // Membership is the run-scoped raw/episode evidence union: it avoids unrelated
  // global devices and retains raw-only devices without derived episodes.
  const char* sql =
      "WITH completed_session AS ("
      "SELECT run_id FROM collector_runs WHERE run_id=? AND ended_wallclock_us IS NOT NULL"
      "), device_ids AS ("
      "SELECT DISTINCT device_id FROM raw_input_events "
      "WHERE run_id=(SELECT run_id FROM completed_session) "
      "UNION SELECT DISTINCT device_id FROM movement_episodes "
      "WHERE run_id=(SELECT run_id FROM completed_session)"
      ") "
      "SELECT ids.device_id, d.device_name, "
      "(SELECT count(*) FROM raw_input_events e WHERE e.run_id=(SELECT run_id FROM completed_session) AND e.device_id=ids.device_id AND e.event_type='MOTION'), "
      "(SELECT count(*) FROM movement_episodes e WHERE e.run_id=(SELECT run_id FROM completed_session) AND e.device_id=ids.device_id), "
      "(SELECT sum(e.device_path_distance) FROM movement_episodes e WHERE e.run_id=(SELECT run_id FROM completed_session) AND e.device_id=ids.device_id), "
      "(SELECT count(e.device_path_distance) FROM movement_episodes e WHERE e.run_id=(SELECT run_id FROM completed_session) AND e.device_id=ids.device_id), "
      "(SELECT count(*) - count(e.device_path_distance) FROM movement_episodes e WHERE e.run_id=(SELECT run_id FROM completed_session) AND e.device_id=ids.device_id), "
      "(SELECT sum(e.compositor_path_distance) FROM movement_episodes e WHERE e.run_id=(SELECT run_id FROM completed_session) AND e.device_id=ids.device_id), "
      "(SELECT count(e.compositor_path_distance) FROM movement_episodes e WHERE e.run_id=(SELECT run_id FROM completed_session) AND e.device_id=ids.device_id), "
      "(SELECT count(*) - count(e.compositor_path_distance) FROM movement_episodes e WHERE e.run_id=(SELECT run_id FROM completed_session) AND e.device_id=ids.device_id) "
      "FROM device_ids ids LEFT JOIN devices d ON d.device_id=ids.device_id "
      "ORDER BY ids.device_id";
  sqlite3_stmt* statement = nullptr;
  if (!prepare(database_, sql, statement, error)) return result;
  if (!bind_id(statement, 1, session_id, error, database_)) {
    sqlite3_finalize(statement);
    return result;
  }
  int status = SQLITE_OK;
  while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
    DeviceSessionSummary device;
    device.device_id = required_text(statement, 0);
    device.device_name = optional_value<std::string>(statement, 1);
    device.raw_motion_count = sqlite3_column_int64(statement, 2);
    device.episode_count = sqlite3_column_int64(statement, 3);
    device.device_path_distance_sum = optional_value<double>(statement, 4);
    device.device_path_distance_available_count = sqlite3_column_int64(statement, 5);
    device.device_path_distance_unavailable_count = sqlite3_column_int64(statement, 6);
    device.compositor_path_distance_sum = optional_value<double>(statement, 7);
    device.compositor_path_distance_available_count = sqlite3_column_int64(statement, 8);
    device.compositor_path_distance_unavailable_count = sqlite3_column_int64(statement, 9);
    result.push_back(std::move(device));
  }
  if (status != SQLITE_DONE) error = sqlite_error(database_, "could not read session device summaries");
  sqlite3_finalize(statement);
  return result;
}

std::optional<CompletedRunSummary> QueryRepository::latest_completed_run(
    std::string& error) const {
  const auto runs = completed_runs(error);
  if (!error.empty() || runs.empty()) return std::nullopt;
  return runs.front();
}

std::vector<EpisodeSummary> QueryRepository::episodes_for_run(std::int64_t run_id,
                                                             std::string& error) const {
  error.clear();
  std::vector<EpisodeSummary> result;
  const char* sql =
      "SELECT e.episode_id, e.run_id, e.device_id, d.device_name, e.start_time_us, "
      "e.end_time_us, e.duration_us, e.end_reason, e.terminates_in_button_press, "
      "e.motion_event_count, (SELECT count(*) FROM movement_episode_members m "
      "WHERE m.episode_id=e.episode_id), e.device_path_distance, e.device_metric_status, "
      "e.device_average_velocity, e.device_peak_velocity, "
      "e.device_directional_reversal_count, e.compositor_path_distance, "
      "e.compositor_displacement, e.compositor_path_efficiency, "
      "e.compositor_average_velocity, e.compositor_peak_velocity, e.compositor_metric_status "
      "FROM movement_episodes e LEFT JOIN devices d ON d.device_id=e.device_id "
      "WHERE e.run_id=? ORDER BY e.start_time_us, e.end_time_us, e.episode_id, e.device_id";
  sqlite3_stmt* statement = nullptr;
  if (!prepare(database_, sql, statement, error) ||
      !bind_id(statement, 1, run_id, error, database_)) {
    sqlite3_finalize(statement);
    return result;
  }
  int status = SQLITE_OK;
  while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
    EpisodeSummary episode;
    episode.episode_id = sqlite3_column_int64(statement, 0);
    episode.run_id = sqlite3_column_int64(statement, 1);
    episode.device_id = required_text(statement, 2);
    episode.device_name = optional_value<std::string>(statement, 3);
    episode.start_time_us = sqlite3_column_int64(statement, 4);
    episode.end_time_us = sqlite3_column_int64(statement, 5);
    episode.duration_us = sqlite3_column_int64(statement, 6);
    episode.end_reason = required_text(statement, 7);
    episode.terminates_in_button_press = sqlite3_column_int(statement, 8) != 0;
    episode.motion_event_count = sqlite3_column_int64(statement, 9);
    episode.total_member_count = sqlite3_column_int64(statement, 10);
    episode.device_path_distance = optional_value<double>(statement, 11);
    episode.device_metric_status = required_text(statement, 12);
    episode.device_average_velocity = optional_value<double>(statement, 13);
    episode.device_peak_velocity = optional_value<double>(statement, 14);
    episode.device_directional_reversal_count = optional_value<std::int64_t>(statement, 15);
    episode.compositor_path_distance = optional_value<double>(statement, 16);
    episode.compositor_displacement = optional_value<double>(statement, 17);
    episode.compositor_path_efficiency = optional_value<double>(statement, 18);
    episode.compositor_average_velocity = optional_value<double>(statement, 19);
    episode.compositor_peak_velocity = optional_value<double>(statement, 20);
    episode.compositor_metric_status = required_text(statement, 21);
    result.push_back(std::move(episode));
  }
  if (status != SQLITE_DONE) error = sqlite_error(database_, "could not read episodes");
  sqlite3_finalize(statement);
  return result;
}

std::vector<TrajectoryPoint> QueryRepository::trajectory_for_episode(
    std::int64_t episode_id, std::string& error) const {
  error.clear();
  std::vector<TrajectoryPoint> result;
  const char* sql =
      "SELECT p.ordinal, p.raw_event_id, p.match_id, p.context_id, p.source_time_us, "
      "p.device_dx, p.device_dy, p.device_cumulative_x, p.device_cumulative_y, "
      "p.device_cumulative_path, p.context_sample_time_us, p.compositor_x, p.compositor_y, "
      "p.compositor_cumulative_path, m.match_status, c.sample_status "
      "FROM movement_episode_trajectory_points p "
      "LEFT JOIN input_context_matches m ON m.match_id=p.match_id "
      "LEFT JOIN pointer_context c ON c.context_id=p.context_id "
      "WHERE p.episode_id=? ORDER BY p.ordinal";
  sqlite3_stmt* statement = nullptr;
  if (!prepare(database_, sql, statement, error) ||
      !bind_id(statement, 1, episode_id, error, database_)) {
    sqlite3_finalize(statement);
    return result;
  }
  int status = SQLITE_OK;
  while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
    TrajectoryPoint point;
    point.ordinal = sqlite3_column_int64(statement, 0);
    point.raw_event_id = sqlite3_column_int64(statement, 1);
    point.match_id = optional_value<std::int64_t>(statement, 2);
    point.context_id = optional_value<std::int64_t>(statement, 3);
    point.source_time_us = optional_value<std::int64_t>(statement, 4);
    point.device_dx = optional_value<double>(statement, 5);
    point.device_dy = optional_value<double>(statement, 6);
    point.device_cumulative_x = optional_value<double>(statement, 7);
    point.device_cumulative_y = optional_value<double>(statement, 8);
    point.device_cumulative_path = optional_value<double>(statement, 9);
    point.context_sample_time_us = optional_value<std::int64_t>(statement, 10);
    point.compositor_x = optional_value<double>(statement, 11);
    point.compositor_y = optional_value<double>(statement, 12);
    point.compositor_cumulative_path = optional_value<double>(statement, 13);
    point.match_status = optional_value<std::string>(statement, 14);
    point.context_sample_status = optional_value<std::string>(statement, 15);
    result.push_back(std::move(point));
  }
  if (status != SQLITE_DONE) error = sqlite_error(database_, "could not read trajectory");
  sqlite3_finalize(statement);
  return result;
}

sqlite3* QueryRepository::native_handle_for_testing() const {
  return database_;
}
