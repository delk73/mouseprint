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
  session_records_.clear();
  episode_records_.clear();
  has_run_ = false;
  error_message_.clear();
  empty_message_.clear();
  sessions_.clear();
  selected_session_.clear();
  selected_session_index_ = -1;
  selected_session_devices_.clear();
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

  session_records_ = repository_->completed_sessions(error);
  if (!error.empty()) {
    setError(QString::fromStdString(error));
    return;
  }
  for (const SessionSummary& session : session_records_) {
    sessions_.append(session_map(session));
  }
  if (session_records_.empty()) {
    empty_message_ = QStringLiteral("No completed session is available.");
    emit stateChanged();
    return;
  }

  if (loadSelectedSession(0)) emit stateChanged();
}

void InspectionController::selectSession(int index) {
  if (!repository_ || index < 0 || index >= static_cast<int>(session_records_.size())) return;
  if (loadSelectedSession(index)) emit stateChanged();
}

bool InspectionController::loadSelectedSession(int index) {
  clearActiveState();
  error_message_.clear();
  selected_session_index_ = index;
  const SessionSummary& session = session_records_[static_cast<std::size_t>(index)];
  selected_session_ = session_map(session);
  run_summary_ = selected_session_;
  has_run_ = true;

  std::string error;
  const auto devices = repository_->device_summaries_for_session(session.session_id, error);
  if (!error.empty()) {
    setError(QString::fromStdString(error));
    return false;
  }
  for (const DeviceSessionSummary& device : devices) {
    selected_session_devices_.append(device_session_map(device));
  }

  episode_records_ = repository_->episodes_for_run(session.run_id, error);
  if (!error.empty()) {
    setError(QString::fromStdString(error));
    return false;
  }
  for (const EpisodeSummary& episode : episode_records_) {
    episodes_.append(episode_map(episode));
  }
  if (episode_records_.empty()) {
    empty_message_ = QStringLiteral("The selected session has no movement episodes.");
    return true;
  }

  empty_message_.clear();
  selected_episode_index_ = 0;
  selected_episode_ = episode_map(episode_records_.front());
  loadTrajectory(episode_records_.front().episode_id);
  return has_run_;
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

QVariantList InspectionController::status_counts_map(const std::vector<StatusCount>& counts) {
  QVariantList result;
  for (const StatusCount& status : counts) {
    QVariantMap item;
    item.insert(QStringLiteral("status"), QString::fromStdString(status.status));
    item.insert(QStringLiteral("count"), QVariant::fromValue<qlonglong>(status.count));
    result.append(item);
  }
  return result;
}

QVariantMap InspectionController::session_map(const SessionSummary& session) {
  QVariantMap result;
  result.insert(QStringLiteral("sessionId"), QVariant::fromValue<qlonglong>(session.session_id));
  result.insert(QStringLiteral("runId"), QVariant::fromValue<qlonglong>(session.run_id));
  result.insert(QStringLiteral("startText"),
                QDateTime::fromMSecsSinceEpoch(session.started_wallclock_us / 1000)
                    .toString(Qt::ISODateWithMs));
  result.insert(QStringLiteral("endText"),
                session.ended_wallclock_us
                    ? QDateTime::fromMSecsSinceEpoch(*session.ended_wallclock_us / 1000)
                          .toString(Qt::ISODateWithMs)
                    : QStringLiteral("-"));
  result.insert(QStringLiteral("durationText"),
                session.display_duration_us ? duration_text(*session.display_duration_us)
                                            : QStringLiteral("-"));
  result.insert(QStringLiteral("rawMotionCount"),
                QVariant::fromValue<qlonglong>(session.raw_motion_count));
  result.insert(QStringLiteral("episodeCount"),
                QVariant::fromValue<qlonglong>(session.movement_episode_count));
  result.insert(QStringLiteral("matched"),
                QVariant::fromValue<qlonglong>(session.correlation_counts.matched));
  result.insert(QStringLiteral("unmatchedContextError"),
                QVariant::fromValue<qlonglong>(session.correlation_counts.unmatched_context_error));
  result.insert(QStringLiteral("unmatchedOutsideTolerance"),
                QVariant::fromValue<qlonglong>(session.correlation_counts.unmatched_outside_tolerance));
  result.insert(QStringLiteral("unmatchedNoContext"),
                QVariant::fromValue<qlonglong>(session.correlation_counts.unmatched_no_context));
  result.insert(QStringLiteral("deviceMetricStatusCounts"),
                status_counts_map(session.device_metric_status_counts));
  result.insert(QStringLiteral("compositorMetricStatusCounts"),
                status_counts_map(session.compositor_metric_status_counts));
  return result;
}

QVariantMap InspectionController::device_session_map(const DeviceSessionSummary& device) {
  QVariantMap result;
  result.insert(QStringLiteral("deviceId"), QString::fromStdString(device.device_id));
  result.insert(QStringLiteral("deviceName"),
                device.device_name ? QString::fromStdString(*device.device_name)
                                    : QString::fromStdString(device.device_id));
  result.insert(QStringLiteral("rawMotionCount"),
                QVariant::fromValue<qlonglong>(device.raw_motion_count));
  result.insert(QStringLiteral("episodeCount"),
                QVariant::fromValue<qlonglong>(device.episode_count));
  result.insert(QStringLiteral("devicePathSum"), optional_number_text(device.device_path_distance_sum));
  result.insert(QStringLiteral("devicePathAvailableCount"),
                QVariant::fromValue<qlonglong>(device.device_path_distance_available_count));
  result.insert(QStringLiteral("devicePathUnavailableCount"),
                QVariant::fromValue<qlonglong>(device.device_path_distance_unavailable_count));
  result.insert(QStringLiteral("compositorPathSum"),
                optional_number_text(device.compositor_path_distance_sum));
  result.insert(QStringLiteral("compositorPathAvailableCount"),
                QVariant::fromValue<qlonglong>(device.compositor_path_distance_available_count));
  result.insert(QStringLiteral("compositorPathUnavailableCount"),
                QVariant::fromValue<qlonglong>(device.compositor_path_distance_unavailable_count));
  return result;
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
  clearActiveState();
  has_run_ = false;
  error_message_ = message;
  emit stateChanged();
}

void InspectionController::clearActiveState() {
  has_run_ = false;
  selected_session_.clear();
  selected_session_index_ = -1;
  selected_session_devices_.clear();
  run_summary_.clear();
  episode_records_.clear();
  episodes_.clear();
  selected_episode_index_ = -1;
  selected_episode_.clear();
  trajectory_points_.clear();
  empty_message_.clear();
}
