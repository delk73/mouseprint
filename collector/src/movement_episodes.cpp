#include "movement_episodes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <optional>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <sqlite3.h>

namespace {

constexpr std::uint64_t idle_gap_us = 100000;

struct Evidence {
  sqlite3_int64 event_id = 0;
  sqlite3_int64 run_id = 0;
  std::uint64_t time_us = 0;
  std::string device_id;
  std::string event_type;
  bool has_unaccelerated = false;
  double dx = 0;
  double dy = 0;
  sqlite3_int64 match_id = 0;
  std::string match_status;
  sqlite3_int64 context_id = 0;
  bool has_cursor = false;
  double cursor_x = 0;
  double cursor_y = 0;
  bool has_monitor = false;
  int monitor_id = 0;
  bool has_workspace = false;
  int workspace_id = 0;
  std::uint64_t context_time_us = 0;
};

struct Member {
  const Evidence* evidence = nullptr;
  std::string role;
};

struct Episode {
  sqlite3_int64 run_id = 0;
  std::string device_id;
  std::vector<Member> members;
  std::uint64_t start_time_us = 0;
  std::uint64_t end_time_us = 0;
  std::string end_reason;
  bool other_device_motion = false;
};

bool exec(sqlite3* db, const char* sql) {
  return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool create_schema(sqlite3* db) {
  return exec(db, "CREATE TABLE IF NOT EXISTS movement_episodes ("
                 "episode_id INTEGER PRIMARY KEY,"
                 "run_id INTEGER NOT NULL REFERENCES collector_runs(run_id),"
                 "device_id TEXT NOT NULL REFERENCES devices(device_id),"
                 "start_time_us INTEGER NOT NULL, end_time_us INTEGER NOT NULL,"
                 "duration_us INTEGER NOT NULL, idle_gap_us INTEGER NOT NULL,"
                 "end_reason TEXT NOT NULL, motion_event_count INTEGER NOT NULL,"
                 "terminates_in_button_press INTEGER NOT NULL,"
                 "device_metric_status TEXT NOT NULL,"
                 "device_path_distance REAL, device_average_velocity REAL,"
                 "device_peak_velocity REAL, device_directional_reversal_count INTEGER,"
                 "compositor_metric_status TEXT NOT NULL,"
                 "compositor_start_x REAL, compositor_start_y REAL,"
                 "compositor_end_x REAL, compositor_end_y REAL,"
                 "compositor_path_distance REAL, compositor_displacement REAL,"
                 "compositor_path_efficiency REAL, compositor_average_velocity REAL,"
                 "compositor_peak_velocity REAL)" ) &&
         exec(db, "CREATE TABLE IF NOT EXISTS movement_episode_members ("
                 "episode_id INTEGER NOT NULL REFERENCES movement_episodes(episode_id),"
                 "ordinal INTEGER NOT NULL, raw_event_id INTEGER NOT NULL "
                 "REFERENCES raw_input_events(event_id),"
                 "match_id INTEGER REFERENCES input_context_matches(match_id),"
                 "member_role TEXT NOT NULL, PRIMARY KEY (episode_id, ordinal))");
}

bool load_evidence(sqlite3* db, std::vector<Evidence>& result) {
  sqlite3_stmt* statement = nullptr;
  const char* sql =
      "SELECT r.event_id, r.run_id, r.source_time_us, r.device_id, r.event_type, "
      "r.dx_unaccelerated, r.dy_unaccelerated, m.match_id, m.match_status, "
      "m.context_id, c.cursor_x, c.cursor_y, c.monitor_id, c.workspace_id, "
      "c.sample_monotonic_us "
      "FROM raw_input_events r LEFT JOIN input_context_matches m "
      "ON m.raw_event_id=r.event_id LEFT JOIN pointer_context c "
      "ON c.context_id=m.context_id "
      "WHERE r.event_type IN ('MOTION','BUTTON_DOWN','BUTTON_UP','SCROLL') "
      "ORDER BY r.run_id, r.source_time_us, r.receive_sequence, r.event_id";
  if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
  int result_code = SQLITE_OK;
  while ((result_code = sqlite3_step(statement)) == SQLITE_ROW) {
    Evidence evidence;
    evidence.event_id = sqlite3_column_int64(statement, 0);
    evidence.run_id = sqlite3_column_int64(statement, 1);
    evidence.time_us = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 2));
    evidence.device_id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
    evidence.event_type = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
    evidence.has_unaccelerated = sqlite3_column_type(statement, 5) != SQLITE_NULL &&
                                 sqlite3_column_type(statement, 6) != SQLITE_NULL;
    if (evidence.has_unaccelerated) {
      evidence.dx = sqlite3_column_double(statement, 5);
      evidence.dy = sqlite3_column_double(statement, 6);
    }
    evidence.match_id = sqlite3_column_int64(statement, 7);
    if (sqlite3_column_type(statement, 8) != SQLITE_NULL) {
      evidence.match_status = reinterpret_cast<const char*>(sqlite3_column_text(statement, 8));
    }
    evidence.context_id = sqlite3_column_int64(statement, 9);
    evidence.has_cursor = sqlite3_column_type(statement, 10) != SQLITE_NULL &&
                          sqlite3_column_type(statement, 11) != SQLITE_NULL;
    if (evidence.has_cursor) {
      evidence.cursor_x = sqlite3_column_double(statement, 10);
      evidence.cursor_y = sqlite3_column_double(statement, 11);
    }
    evidence.has_monitor = sqlite3_column_type(statement, 12) != SQLITE_NULL;
    evidence.has_workspace = sqlite3_column_type(statement, 13) != SQLITE_NULL;
    if (evidence.has_monitor) evidence.monitor_id = sqlite3_column_int(statement, 12);
    if (evidence.has_workspace) evidence.workspace_id = sqlite3_column_int(statement, 13);
    if (sqlite3_column_type(statement, 14) != SQLITE_NULL) {
      evidence.context_time_us = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 14));
    }
    result.push_back(std::move(evidence));
  }
  const bool successful = result_code == SQLITE_DONE;
  sqlite3_finalize(statement);
  return successful;
}

void close_episode(std::optional<Episode>& active, std::vector<Episode>& episodes,
                   std::uint64_t end_time, const char* reason, bool button) {
  if (!active) return;
  active->end_time_us = end_time;
  active->end_reason = reason;
  if (button) active->members.back().role = "terminating_button_down";
  episodes.push_back(std::move(*active));
  active.reset();
}

std::vector<Episode> segment(const std::vector<Evidence>& evidence) {
  std::vector<Episode> episodes;
  std::map<std::pair<sqlite3_int64, std::string>, std::optional<Episode>> active;
  for (const Evidence& item : evidence) {
    const auto key = std::make_pair(item.run_id, item.device_id);
    auto& current = active[key];
    if (item.event_type == "MOTION") {
      if (current && item.time_us - current->members.back().evidence->time_us > idle_gap_us) {
        close_episode(current, episodes, current->members.back().evidence->time_us,
                      "idle_gap", false);
      }
      if (!current) {
        current = Episode{item.run_id, item.device_id, {}, item.time_us, item.time_us, "", false};
      }
      current->members.push_back({&item, "motion"});
      current->end_time_us = item.time_us;
    } else if (item.event_type == "BUTTON_DOWN") {
      if (current) {
        if (item.time_us - current->members.back().evidence->time_us > idle_gap_us) {
          close_episode(current, episodes, current->members.back().evidence->time_us,
                        "idle_gap", false);
          continue;
        }
        current->members.push_back({&item, "terminating_button_down"});
        close_episode(current, episodes, item.time_us, "button_down", true);
      }
    }
  }
  for (auto& entry : active) {
    if (entry.second) {
      close_episode(entry.second, episodes,
                    entry.second->members.back().evidence->time_us, "run_end", false);
    }
  }
  std::sort(episodes.begin(), episodes.end(), [](const Episode& left, const Episode& right) {
    return std::tie(left.run_id, left.start_time_us, left.device_id) <
           std::tie(right.run_id, right.start_time_us, right.device_id);
  });
  for (Episode& episode : episodes) {
    const auto first = episode.members.front().evidence->time_us;
    const auto last_motion = std::find_if(episode.members.rbegin(), episode.members.rend(),
                                          [](const Member& member) {
                                            return member.role == "motion";
                                          })->evidence->time_us;
    for (const Evidence& item : evidence) {
      if (item.run_id == episode.run_id && item.event_type == "MOTION" &&
          item.device_id != episode.device_id && item.time_us > first &&
          item.time_us < last_motion) {
        episode.other_device_motion = true;
        break;
      }
    }
  }
  return episodes;
}

struct Metrics {
  std::string device_status = "available";
  std::optional<double> device_path, device_average, device_peak;
  std::optional<int> reversals;
  std::string compositor_status = "available";
  std::optional<double> start_x, start_y, end_x, end_y, path, displacement, efficiency,
      compositor_average, compositor_peak;
};

Metrics calculate(const Episode& episode) {
  Metrics metrics;
  std::vector<const Evidence*> motions;
  for (const Member& member : episode.members) {
    if (member.role == "motion") motions.push_back(member.evidence);
  }
  bool device_complete = true;
  double path = 0;
  for (const Evidence* motion : motions) {
    if (!motion->has_unaccelerated) {
      device_complete = false;
      continue;
    }
    path += std::hypot(motion->dx, motion->dy);
  }
  if (!device_complete) {
    metrics.device_status = "missing_unaccelerated_values";
  } else {
    metrics.device_path = path;
    int reversals = 0;
    const Evidence* previous = nullptr;
    std::vector<double> velocities;
    for (const Evidence* motion : motions) {
      if (previous) {
        const std::uint64_t delta = motion->time_us - previous->time_us;
        if (delta > 0) velocities.push_back(std::hypot(motion->dx, motion->dy) /
                                             (static_cast<double>(delta) / 1000000.0));
      }
      if (previous && (previous->dx != 0 || previous->dy != 0) &&
          (motion->dx != 0 || motion->dy != 0) &&
          previous->dx * motion->dx + previous->dy * motion->dy < 0) {
        ++reversals;
      }
      previous = motion;
    }
    metrics.reversals = reversals;
    if (!velocities.empty()) {
      metrics.device_average = std::accumulate(velocities.begin(), velocities.end(), 0.0) /
                               velocities.size();
      metrics.device_peak = *std::max_element(velocities.begin(), velocities.end());
    }
  }

  std::vector<const Evidence*> contexts;
  sqlite3_int64 previous_context = 0;
  bool invalid = episode.other_device_motion;
  if (invalid) metrics.compositor_status = "other_device_motion";
  for (const Evidence* motion : motions) {
    if (motion->match_status != "matched" || motion->match_id == 0) {
      if (!invalid) metrics.compositor_status = motion->match_status == "unmatched_context_error"
                                                    ? "context_sampling_failed"
                                                    : "unmatched_context";
      invalid = true;
    } else if (!motion->has_cursor) {
      if (!invalid) metrics.compositor_status = "missing_cursor_position";
      invalid = true;
    } else if (motion->context_id != previous_context) {
      contexts.push_back(motion);
      previous_context = motion->context_id;
    }
  }
  for (std::size_t index = 1; index < contexts.size() && !invalid; ++index) {
    if (contexts[index - 1]->has_monitor && contexts[index]->has_monitor &&
        contexts[index - 1]->monitor_id != contexts[index]->monitor_id) {
      metrics.compositor_status = "monitor_transition";
      invalid = true;
    }
    if (contexts[index - 1]->has_workspace && contexts[index]->has_workspace &&
        contexts[index - 1]->workspace_id != contexts[index]->workspace_id) {
      metrics.compositor_status = "workspace_transition";
      invalid = true;
    }
  }
  if (invalid) return metrics;
  if (contexts.empty()) {
    metrics.compositor_status = "missing_cursor_position";
    return metrics;
  }
  metrics.start_x = contexts.front()->cursor_x;
  metrics.start_y = contexts.front()->cursor_y;
  metrics.end_x = contexts.back()->cursor_x;
  metrics.end_y = contexts.back()->cursor_y;
  double compositor_path = 0;
  std::vector<double> velocities;
  for (std::size_t index = 1; index < contexts.size(); ++index) {
    const double step = std::hypot(contexts[index]->cursor_x - contexts[index - 1]->cursor_x,
                                   contexts[index]->cursor_y - contexts[index - 1]->cursor_y);
    compositor_path += step;
    const std::uint64_t delta = contexts[index]->context_time_us -
                                contexts[index - 1]->context_time_us;
    if (delta > 0) velocities.push_back(step / (static_cast<double>(delta) / 1000000.0));
  }
  metrics.path = compositor_path;
  metrics.displacement = std::hypot(*metrics.end_x - *metrics.start_x,
                                    *metrics.end_y - *metrics.start_y);
  if (compositor_path > 0) metrics.efficiency = *metrics.displacement / compositor_path;
  if (!velocities.empty()) {
    metrics.compositor_average = std::accumulate(velocities.begin(), velocities.end(), 0.0) /
                                 velocities.size();
    metrics.compositor_peak = *std::max_element(velocities.begin(), velocities.end());
  }
  return metrics;
}

bool persist(sqlite3* db, const std::vector<Episode>& episodes) {
  const char* episode_sql =
      "INSERT INTO movement_episodes (run_id,device_id,start_time_us,end_time_us,duration_us,"
      "idle_gap_us,end_reason,motion_event_count,terminates_in_button_press,device_metric_status,"
      "device_path_distance,device_average_velocity,device_peak_velocity,"
      "device_directional_reversal_count,compositor_metric_status,compositor_start_x,"
      "compositor_start_y,compositor_end_x,compositor_end_y,compositor_path_distance,"
      "compositor_displacement,compositor_path_efficiency,compositor_average_velocity,"
      "compositor_peak_velocity) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
  sqlite3_stmt* episode_statement = nullptr;
  sqlite3_stmt* member_statement = nullptr;
  if (sqlite3_prepare_v2(db, episode_sql, -1, &episode_statement, nullptr) != SQLITE_OK ||
      sqlite3_prepare_v2(db, "INSERT INTO movement_episode_members "
                            "(episode_id,ordinal,raw_event_id,match_id,member_role) "
                            "VALUES (?,?,?,?,?)", -1, &member_statement, nullptr) != SQLITE_OK) {
    sqlite3_finalize(episode_statement);
    sqlite3_finalize(member_statement);
    return false;
  }
  for (const Episode& episode : episodes) {
    const Metrics metrics = calculate(episode);
    sqlite3_bind_int64(episode_statement, 1, episode.run_id);
    sqlite3_bind_text(episode_statement, 2, episode.device_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(episode_statement, 3, episode.start_time_us);
    sqlite3_bind_int64(episode_statement, 4, episode.end_time_us);
    sqlite3_bind_int64(episode_statement, 5, episode.end_time_us - episode.start_time_us);
    sqlite3_bind_int64(episode_statement, 6, idle_gap_us);
    sqlite3_bind_text(episode_statement, 7, episode.end_reason.c_str(), -1, SQLITE_TRANSIENT);
    int motion_count = 0;
    for (const Member& member : episode.members) motion_count += member.role == "motion";
    sqlite3_bind_int(episode_statement, 8, motion_count);
    sqlite3_bind_int(episode_statement, 9, episode.end_reason == "button_down");
    sqlite3_bind_text(episode_statement, 10, metrics.device_status.c_str(), -1, SQLITE_TRANSIENT);
    auto bind_optional = [&](int index, const std::optional<double>& value) {
      if (value) sqlite3_bind_double(episode_statement, index, *value);
      else sqlite3_bind_null(episode_statement, index);
    };
    bind_optional(11, metrics.device_path); bind_optional(12, metrics.device_average);
    bind_optional(13, metrics.device_peak);
    if (metrics.reversals) sqlite3_bind_int(episode_statement, 14, *metrics.reversals);
    else sqlite3_bind_null(episode_statement, 14);
    sqlite3_bind_text(episode_statement, 15, metrics.compositor_status.c_str(), -1, SQLITE_TRANSIENT);
    bind_optional(16, metrics.start_x); bind_optional(17, metrics.start_y);
    bind_optional(18, metrics.end_x); bind_optional(19, metrics.end_y);
    bind_optional(20, metrics.path); bind_optional(21, metrics.displacement);
    bind_optional(22, metrics.efficiency); bind_optional(23, metrics.compositor_average);
    bind_optional(24, metrics.compositor_peak);
    if (sqlite3_step(episode_statement) != SQLITE_DONE) {
      std::cerr << "movement episode insert failed: " << sqlite3_errmsg(db) << "\n";
      return false;
    }
    const sqlite3_int64 episode_id = sqlite3_last_insert_rowid(db);
    sqlite3_reset(episode_statement); sqlite3_clear_bindings(episode_statement);
    int ordinal = 0;
    for (const Member& member : episode.members) {
      sqlite3_bind_int64(member_statement, 1, episode_id);
      sqlite3_bind_int(member_statement, 2, ordinal++);
      sqlite3_bind_int64(member_statement, 3, member.evidence->event_id);
      if (member.evidence->match_id) sqlite3_bind_int64(member_statement, 4, member.evidence->match_id);
      else sqlite3_bind_null(member_statement, 4);
      sqlite3_bind_text(member_statement, 5, member.role.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(member_statement) != SQLITE_DONE) {
        std::cerr << "movement member insert failed: " << sqlite3_errmsg(db) << "\n";
        return false;
      }
      sqlite3_reset(member_statement); sqlite3_clear_bindings(member_statement);
    }
  }
  sqlite3_finalize(episode_statement); sqlite3_finalize(member_statement);
  return true;
}

}  // namespace

bool derive_movement_episodes(sqlite3* db) {
  if (!db || !create_schema(db)) {
    if (db) std::cerr << "movement schema failed: " << sqlite3_errmsg(db) << "\n";
    return false;
  }
  if (!exec(db, "BEGIN TRANSACTION")) {
    std::cerr << "movement transaction start failed: " << sqlite3_errmsg(db) << "\n";
    return false;
  }
  const auto rollback = [&]() {
    exec(db, "ROLLBACK");
    return false;
  };
  if (!exec(db, "DELETE FROM movement_episode_members") ||
      !exec(db, "DELETE FROM movement_episodes")) {
    std::cerr << "movement cleanup failed: " << sqlite3_errmsg(db) << "\n";
    return rollback();
  }
  std::vector<Evidence> evidence;
  if (!load_evidence(db, evidence)) {
    std::cerr << "movement evidence load failed: " << sqlite3_errmsg(db) << "\n";
    return rollback();
  }
  if (!persist(db, segment(evidence))) return rollback();
  if (!exec(db, "COMMIT")) {
    std::cerr << "movement transaction commit failed: " << sqlite3_errmsg(db) << "\n";
    return rollback();
  }
  return true;
}
