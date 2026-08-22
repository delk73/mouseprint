#ifndef MOUSEPRINT_INSPECTION_CONTROLLER_H
#define MOUSEPRINT_INSPECTION_CONTROLLER_H

#include "query_repository.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <memory>

class InspectionController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool hasRun READ hasRun NOTIFY stateChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
  Q_PROPERTY(QString emptyMessage READ emptyMessage NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap runSummary READ runSummary NOTIFY stateChanged)
  Q_PROPERTY(QVariantList episodes READ episodes NOTIFY stateChanged)
  Q_PROPERTY(int selectedEpisodeIndex READ selectedEpisodeIndex NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap selectedEpisode READ selectedEpisode NOTIFY stateChanged)
  Q_PROPERTY(QVariantList trajectoryPoints READ trajectoryPoints NOTIFY stateChanged)

 public:
  explicit InspectionController(QObject* parent = nullptr);

  void load(const QString& databasePath);

  bool hasRun() const { return has_run_; }
  QString errorMessage() const { return error_message_; }
  QString emptyMessage() const { return empty_message_; }
  QVariantMap runSummary() const { return run_summary_; }
  QVariantList episodes() const { return episodes_; }
  int selectedEpisodeIndex() const { return selected_episode_index_; }
  QVariantMap selectedEpisode() const { return selected_episode_; }
  QVariantList trajectoryPoints() const { return trajectory_points_; }

  Q_INVOKABLE void selectEpisode(int index);

 signals:
  void stateChanged();

 private:
  static QVariantMap episode_map(const EpisodeSummary& episode);
  static QVariantMap trajectory_map(const TrajectoryPoint& point);
  static QString duration_text(std::int64_t duration_us);
  static QString monotonic_time_text(std::int64_t time_us);
  static QString optional_number_text(const std::optional<double>& value);
  static QString optional_integer_text(const std::optional<std::int64_t>& value);

  void setError(const QString& message);
  void loadTrajectory(std::int64_t episode_id);

  std::unique_ptr<QueryRepository> repository_;
  std::vector<EpisodeSummary> episode_records_;
  bool has_run_ = false;
  QString error_message_;
  QString empty_message_;
  QVariantMap run_summary_;
  QVariantList episodes_;
  int selected_episode_index_ = -1;
  QVariantMap selected_episode_;
  QVariantList trajectory_points_;
};

#endif
