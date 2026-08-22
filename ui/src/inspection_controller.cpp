#include "inspection_controller.h"

#include <QDateTime>

#include <QStringList>

namespace {

QVariant optional_integer(const std::optional<std::int64_t>& value) {
  return value ? QVariant::fromValue<qlonglong>(*value) : QVariant();
}

QVariant optional_double(const std::optional<double>& value) {
  return value ? QVariant(*value) : QVariant();
}

QString display_or_unavailable(const QString& value) {
  return value.isEmpty() ? QStringLiteral("-") : value;
}

}  // namespace

InspectionController::InspectionController(QObject* parent) : QObject(parent) {}

void InspectionController::load(const QString& databasePath) {
  repository_.reset();
  episode_records_.clear();
  has_run_ = false;
  error_message_.clear();
  empty_message_.clear();
  run_summary_.clear();
  episodes_.clear();
  selected_episode_index_ = -1;
  selected_episode_.clear();
  trajectory_points_.clear();

  std::string error;
  repository_ = QueryRepository::open(databasePath.toStdString(), error);
  if (!repository_) {
    setError(QString::fromStdString(error));
    return;
  }

  const auto latest = repository_->latest_completed_run(error);
  if (!error.empty()) {
    setError(QString::fromStdString(error));
    return;
  }
  if (!latest) {
    empty_message_ = QStringLiteral("No completed run is available.");
    emit stateChanged();
    return;
  }

  has_run_ = true;
  run_summary_.insert(QStringLiteral("runId"), QVariant::fromValue<qlonglong>(latest->run_id));
  run_summary_.insert(QStringLiteral("startText"),
                      QDateTime::fromMSecsSinceEpoch(latest->started_wallclock_us / 1000)
                          .toString(Qt::ISODateWithMs));
  run_summary_.insert(
      QStringLiteral("endText"),
      latest->ended_wallclock_us
          ? QDateTime::fromMSecsSinceEpoch(*latest->ended_wallclock_us / 1000)
                .toString(Qt::ISODateWithMs)
          : QStringLiteral("-"));
  run_summary_.insert(QStringLiteral("durationText"),
                      latest->display_duration_us
                          ? duration_text(*latest->display_duration_us)
                          : QStringLiteral("-"));
  run_summary_.insert(QStringLiteral("rawMotionCount"),
                      QVariant::fromValue<qlonglong>(latest->raw_motion_count));
  run_summary_.insert(QStringLiteral("episodeCount"),
                      QVariant::fromValue<qlonglong>(latest->movement_episode_count));
  run_summary_.insert(QStringLiteral("matched"),
                      QVariant::fromValue<qlonglong>(latest->correlation_counts.matched));
  run_summary_.insert(
      QStringLiteral("unmatchedContextError"),
      QVariant::fromValue<qlonglong>(latest->correlation_counts.unmatched_context_error));
  run_summary_.insert(
      QStringLiteral("unmatchedOutsideTolerance"),
      QVariant::fromValue<qlonglong>(latest->correlation_counts.unmatched_outside_tolerance));
  run_summary_.insert(
      QStringLiteral("unmatchedNoContext"),
      QVariant::fromValue<qlonglong>(latest->correlation_counts.unmatched_no_context));

  episode_records_ = repository_->episodes_for_run(latest->run_id, error);
  if (!error.empty()) {
    setError(QString::fromStdString(error));
    return;
  }
  for (const EpisodeSummary& episode : episode_records_) {
    episodes_.append(episode_map(episode));
  }
  if (episode_records_.empty()) {
    empty_message_ = QStringLiteral("The latest completed run has no movement episodes.");
  } else {
    selected_episode_index_ = 0;
    selected_episode_ = episode_map(episode_records_.front());
    loadTrajectory(episode_records_.front().episode_id);
  }
  emit stateChanged();
}

void InspectionController::selectEpisode(int index) {
  if (index < 0 || index >= static_cast<int>(episode_records_.size())) return;
  selected_episode_index_ = index;
  selected_episode_ = episode_map(episode_records_[static_cast<std::size_t>(index)]);
  loadTrajectory(episode_records_[static_cast<std::size_t>(index)].episode_id);
  emit stateChanged();
}

void InspectionController::loadTrajectory(std::int64_t episode_id) {
  trajectory_points_.clear();
  std::string error;
  const auto points = repository_->trajectory_for_episode(episode_id, error);
  if (!error.empty()) {
    setError(QString::fromStdString(error));
    return;
  }
  for (const TrajectoryPoint& point : points) {
    trajectory_points_.append(trajectory_map(point));
  }
}

QVariantMap InspectionController::episode_map(const EpisodeSummary& episode) {
  QVariantMap result;
  result.insert(QStringLiteral("episodeId"), QVariant::fromValue<qlonglong>(episode.episode_id));
  result.insert(QStringLiteral("device"),
               episode.device_name ? QStringLiteral("%1 (%2)")
                                          .arg(QString::fromStdString(*episode.device_name),
                                               QString::fromStdString(episode.device_id))
                                    : QString::fromStdString(episode.device_id));
  result.insert(QStringLiteral("startText"), monotonic_time_text(episode.start_time_us));
  result.insert(QStringLiteral("endText"), monotonic_time_text(episode.end_time_us));
  result.insert(QStringLiteral("durationText"), duration_text(episode.duration_us));
  result.insert(QStringLiteral("endReason"), QString::fromStdString(episode.end_reason));
  result.insert(QStringLiteral("motionCount"),
               QVariant::fromValue<qlonglong>(episode.motion_event_count));
  result.insert(QStringLiteral("devicePath"), optional_number_text(episode.device_path_distance));
  result.insert(QStringLiteral("deviceAverage"),
               optional_number_text(episode.device_average_velocity));
  result.insert(QStringLiteral("devicePeak"), optional_number_text(episode.device_peak_velocity));
  result.insert(QStringLiteral("reversals"),
               optional_integer_text(episode.device_directional_reversal_count));
  result.insert(QStringLiteral("compositorPath"),
               optional_number_text(episode.compositor_path_distance));
  result.insert(QStringLiteral("displacement"),
               optional_number_text(episode.compositor_displacement));
  result.insert(QStringLiteral("efficiency"),
               optional_number_text(episode.compositor_path_efficiency));
  result.insert(QStringLiteral("compositorAverage"),
               optional_number_text(episode.compositor_average_velocity));
  result.insert(QStringLiteral("compositorPeak"),
               optional_number_text(episode.compositor_peak_velocity));
  result.insert(QStringLiteral("deviceStatus"),
               display_or_unavailable(QString::fromStdString(episode.device_metric_status)));
  result.insert(QStringLiteral("compositorStatus"), display_or_unavailable(
                                                    QString::fromStdString(episode.compositor_metric_status)));
  result.insert(QStringLiteral("terminatesInButtonPress"), episode.terminates_in_button_press);
  return result;
}

QVariantMap InspectionController::trajectory_map(const TrajectoryPoint& point) {
  QVariantMap result;
  result.insert(QStringLiteral("ordinal"), QVariant::fromValue<qlonglong>(point.ordinal));
  result.insert(QStringLiteral("rawEventId"), QVariant::fromValue<qlonglong>(point.raw_event_id));
  result.insert(QStringLiteral("matchId"), optional_integer(point.match_id));
  result.insert(QStringLiteral("contextId"), optional_integer(point.context_id));
  result.insert(QStringLiteral("sourceTimeUs"), optional_integer(point.source_time_us));
  result.insert(QStringLiteral("deviceDx"), optional_double(point.device_dx));
  result.insert(QStringLiteral("deviceDy"), optional_double(point.device_dy));
  result.insert(QStringLiteral("deviceX"), optional_double(point.device_cumulative_x));
  result.insert(QStringLiteral("deviceY"), optional_double(point.device_cumulative_y));
  result.insert(QStringLiteral("devicePath"), optional_double(point.device_cumulative_path));
  result.insert(QStringLiteral("contextSampleTimeUs"), optional_integer(point.context_sample_time_us));
  result.insert(QStringLiteral("compositorX"), optional_double(point.compositor_x));
  result.insert(QStringLiteral("compositorY"), optional_double(point.compositor_y));
  result.insert(QStringLiteral("compositorPath"), optional_double(point.compositor_cumulative_path));
  result.insert(QStringLiteral("matchStatus"),
               point.match_status ? QString::fromStdString(*point.match_status) : QVariant());
  result.insert(QStringLiteral("contextStatus"),
               point.context_sample_status ? QString::fromStdString(*point.context_sample_status)
                                            : QVariant());
  return result;
}

QString InspectionController::duration_text(std::int64_t duration_us) {
  return QStringLiteral("%1 ms").arg(static_cast<double>(duration_us) / 1000.0, 0, 'f', 2);
}

QString InspectionController::monotonic_time_text(std::int64_t time_us) {
  return QStringLiteral("%1 us").arg(time_us);
}

QString InspectionController::optional_number_text(const std::optional<double>& value) {
  return value ? QString::number(*value, 'f', 3) : QStringLiteral("-");
}

QString InspectionController::optional_integer_text(const std::optional<std::int64_t>& value) {
  return value ? QString::number(*value) : QStringLiteral("-");
}

void InspectionController::setError(const QString& message) {
  has_run_ = false;
  error_message_ = message;
  emit stateChanged();
}
