#ifndef MOUSEPRINT_UI_MODELS_H
#define MOUSEPRINT_UI_MODELS_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct CorrelationCounts {
  std::int64_t matched = 0;
  std::int64_t unmatched_context_error = 0;
  std::int64_t unmatched_outside_tolerance = 0;
  std::int64_t unmatched_no_context = 0;
};

struct CompletedRunSummary {
  std::int64_t run_id = 0;
  std::int64_t started_wallclock_us = 0;
  std::optional<std::int64_t> ended_wallclock_us;
  std::optional<std::int64_t> display_duration_us;
  std::int64_t raw_motion_count = 0;
  std::int64_t movement_episode_count = 0;
  CorrelationCounts correlation_counts;
};

struct StatusCount {
  std::string status;
  std::int64_t count = 0;
};

struct SessionSummary {
  std::int64_t session_id = 0;
  std::int64_t run_id = 0;
  std::int64_t started_wallclock_us = 0;
  std::optional<std::int64_t> ended_wallclock_us;
  std::optional<std::int64_t> display_duration_us;
  std::int64_t raw_motion_count = 0;
  std::int64_t movement_episode_count = 0;
  CorrelationCounts correlation_counts;
  std::vector<StatusCount> device_metric_status_counts;
  std::vector<StatusCount> compositor_metric_status_counts;
};

struct DeviceSessionSummary {
  std::string device_id;
  std::optional<std::string> device_name;
  std::int64_t raw_motion_count = 0;
  std::int64_t episode_count = 0;
  std::optional<double> device_path_distance_sum;
  std::int64_t device_path_distance_available_count = 0;
  std::int64_t device_path_distance_unavailable_count = 0;
  std::optional<double> compositor_path_distance_sum;
  std::int64_t compositor_path_distance_available_count = 0;
  std::int64_t compositor_path_distance_unavailable_count = 0;
};

struct EpisodeSummary {
  std::int64_t episode_id = 0;
  std::int64_t run_id = 0;
  std::string device_id;
  std::optional<std::string> device_name;
  std::int64_t start_time_us = 0;
  std::int64_t end_time_us = 0;
  std::int64_t duration_us = 0;
  std::string end_reason;
  bool terminates_in_button_press = false;
  std::int64_t motion_event_count = 0;
  std::int64_t total_member_count = 0;
  std::optional<double> device_path_distance;
  std::string device_metric_status;
  std::optional<double> device_average_velocity;
  std::optional<double> device_peak_velocity;
  std::optional<std::int64_t> device_directional_reversal_count;
  std::optional<double> compositor_path_distance;
  std::optional<double> compositor_displacement;
  std::optional<double> compositor_path_efficiency;
  std::optional<double> compositor_average_velocity;
  std::optional<double> compositor_peak_velocity;
  std::string compositor_metric_status;
};

struct TrajectoryPoint {
  std::int64_t ordinal = 0;
  std::int64_t raw_event_id = 0;
  std::optional<std::int64_t> match_id;
  std::optional<std::int64_t> context_id;
  std::optional<std::int64_t> source_time_us;
  std::optional<double> device_dx;
  std::optional<double> device_dy;
  std::optional<double> device_cumulative_x;
  std::optional<double> device_cumulative_y;
  std::optional<double> device_cumulative_path;
  std::optional<std::int64_t> context_sample_time_us;
  std::optional<double> compositor_x;
  std::optional<double> compositor_y;
  std::optional<double> compositor_cumulative_path;
  std::optional<std::string> match_status;
  std::optional<std::string> context_sample_status;
};

#endif
