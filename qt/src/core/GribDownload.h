// Downloads a regional NOAA GFS 0.25° wind/pressure forecast from NOMADS.
#pragma once

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QRectF>

class QNetworkReply;

class GribDownload : public QObject {
  Q_OBJECT
public:
  explicit GribDownload(QObject* parent = nullptr);

  /** Starts the download for the box (lat/lon degrees). */
  void start(double south, double west, double north, double east,
             int hours = 72, int stepHours = 3);
  void cancel();
  bool isRunning() const { return m_running; }

  /** NOMADS filter URL for one forecast hour of a run (public for tests). */
  static QUrl url(const QDateTime& run, int hour, double south, double west,
                  double north, double east);
  /** Candidate runs, newest first (GFS appears ~4 h after the cycle). */
  static QVector<QDateTime> candidateRuns(const QDateTime& nowUtc);

signals:
  void progress(int done, int total, const QString& text);
  void finished(const QString& path);
  void failed(const QString& error);

private:
  void tryRun();
  void fetchNext();

  QNetworkAccessManager m_net;
  QPointer<QNetworkReply> m_reply;
  QVector<QDateTime> m_runs;
  QDateTime m_run;
  int m_hour = 0, m_hours = 72, m_step = 3;
  double m_s = 0, m_w = 0, m_n = 0, m_e = 0;
  QByteArray m_data;
  bool m_running = false;
};
