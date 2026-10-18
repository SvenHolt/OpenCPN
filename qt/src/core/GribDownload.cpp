#include "core/GribDownload.h"

#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QUrlQuery>
#include <cmath>

GribDownload::GribDownload(QObject* parent) : QObject(parent) {}

QVector<QDateTime> GribDownload::candidateRuns(const QDateTime& nowUtc) {
  QVector<QDateTime> runs;
  QDateTime t = nowUtc.toUTC().addSecs(-4 * 3600);
  t.setTime(QTime(t.time().hour() / 6 * 6, 0));
  for (int i = 0; i < 4; ++i) runs << t.addSecs(-6 * 3600 * i);
  return runs;
}

QUrl GribDownload::url(const QDateTime& run, int hour, double south,
                       double west, double north, double east) {
  QUrl u("https://nomads.ncep.noaa.gov/cgi-bin/filter_gfs_0p25.pl");
  QUrlQuery q;
  const QString hh = run.toString("hh");
  q.addQueryItem(
      "dir",
      QStringLiteral("/gfs.%1/%2/atmos").arg(run.toString("yyyyMMdd"), hh));
  q.addQueryItem("file", QStringLiteral("gfs.t%1z.pgrb2.0p25.f%2")
                             .arg(hh)
                             .arg(hour, 3, 10, QChar('0')));
  for (const char* v : {"var_UGRD", "var_VGRD", "var_PRMSL", "var_GUST",
                        "lev_10_m_above_ground", "lev_mean_sea_level",
                        "lev_surface", "subregion"})
    q.addQueryItem(v, v == QByteArray("subregion") ? "" : "on");
  q.addQueryItem("leftlon", QString::number(std::floor(west)));
  q.addQueryItem("rightlon", QString::number(std::ceil(east)));
  q.addQueryItem("toplat", QString::number(std::ceil(north)));
  q.addQueryItem("bottomlat", QString::number(std::floor(south)));
  u.setQuery(q);
  return u;
}

void GribDownload::start(double south, double west, double north, double east,
                         int hours, int stepHours) {
  cancel();
  m_s = qBound(-90.0, south, 90.0);
  m_n = qBound(-90.0, north, 90.0);
  m_w = west;
  m_e = east;
  m_hours = hours;
  m_step = stepHours;
  m_runs = candidateRuns(QDateTime::currentDateTimeUtc());
  m_data.clear();
  m_running = true;
  tryRun();
}

void GribDownload::cancel() {
  m_running = false;
  if (m_reply) m_reply->abort();
}

void GribDownload::tryRun() {
  if (m_runs.isEmpty()) {
    m_running = false;
    emit failed(tr("No recent GFS run is available"));
    return;
  }
  m_run = m_runs.takeFirst();
  m_hour = 0;
  m_data.clear();
  emit progress(
      0, m_hours / m_step + 1,
      tr("Checking GFS run %1 UTC…").arg(m_run.toString("dd.MM. hh:mm")));
  fetchNext();
}

void GribDownload::fetchNext() {
  if (!m_running) return;
  QNetworkRequest req(url(m_run, m_hour, m_s, m_w, m_n, m_e));
  req.setTransferTimeout(60000);
  m_reply = m_net.get(req);
  connect(m_reply, &QNetworkReply::finished, this, [this] {
    QNetworkReply* r = m_reply;
    r->deleteLater();
    if (!m_running) return;
    const QByteArray body = r->readAll();
    const bool ok =
        r->error() == QNetworkReply::NoError && body.startsWith("GRIB");
    if (!ok) {
      if (m_hour == 0) {  // run not published yet: try the previous one
        tryRun();
      } else {
        m_running = false;
        emit failed(r->error() != QNetworkReply::NoError
                        ? r->errorString()
                        : tr("Unexpected server response"));
      }
      return;
    }
    m_data += body;
    const int total = m_hours / m_step + 1;
    const int done = m_hour / m_step + 1;
    emit progress(done, total,
                  tr("Downloading GFS %1 UTC: +%2 h")
                      .arg(m_run.toString("dd.MM. hh:mm"))
                      .arg(m_hour));
    m_hour += m_step;
    if (m_hour <= m_hours) {
      fetchNext();
      return;
    }
    m_running = false;
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/grib";
    QDir().mkpath(dir);
    const QString path =
        dir + QStringLiteral("/gfs_%1.grb2").arg(m_run.toString("yyyyMMdd_hh"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
      emit failed(f.errorString());
      return;
    }
    f.write(m_data);
    f.close();
    emit finished(path);
  });
}
