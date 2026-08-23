#ifndef MOUSEPRINT_QUERY_REPOSITORY_H
#define MOUSEPRINT_QUERY_REPOSITORY_H

#include "ui_models.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

class QueryRepository {
 public:
  ~QueryRepository();

  static std::unique_ptr<QueryRepository> open(const std::string& path,
                                               std::string& error);

  std::vector<CompletedRunSummary> completed_runs(std::string& error) const;
  std::optional<CompletedRunSummary> latest_completed_run(std::string& error) const;
  std::vector<SessionSummary> completed_sessions(std::string& error) const;
  std::optional<SessionSummary> latest_session(std::string& error) const;
  std::vector<DeviceSessionSummary> device_summaries_for_session(
      std::int64_t session_id, std::string& error) const;
  std::vector<EpisodeSummary> episodes_for_run(std::int64_t run_id,
                                               std::string& error) const;
  std::vector<TrajectoryPoint> trajectory_for_episode(std::int64_t episode_id,
                                                      std::string& error) const;

  // Exposed only so the boundary test can verify the SQLite connection mode.
  sqlite3* native_handle_for_testing() const;

 private:
  explicit QueryRepository(sqlite3* database) : database_(database) {}

  sqlite3* database_ = nullptr;
};

#endif
