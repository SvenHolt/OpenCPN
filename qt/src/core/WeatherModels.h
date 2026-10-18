// Wind forecasts of several weather models along a route (Open-Meteo:
// DWD ICON, NOAA GFS, ECMWF IFS, Météo-France), for comparing passage
// plans. Forecasts can be stored with a voyage and used offline.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointF>
#include <QVector>
#include <functional>

struct ModelForecast {
  QString model;            // Open-Meteo model id
  QVector<QPointF> points;  // x = lon, y = lat
  QVector<qint64> times;    // hourly, unix seconds UTC
  // [point][time]; NaN = missing
  QVector<QVector<float>> speedKn, dirFrom, gustKn;
  QDateTime fetched;

  bool isValid() const { return !points.isEmpty() && times.size() >= 2; }
  QDateTime firstTime() const;
  QDateTime lastTime() const;
  /** Wind at a place and time: weighted between the two nearest sample
   *  points, linear in time. False outside the forecast. */
  bool wind(double lat, double lon, const QDateTime& t, double& kn,
            double& fromDeg, double* gust = nullptr) const;

  QJsonObject toJson() const;
  static ModelForecast fromJson(const QJsonObject& o);
};

struct WeatherModelInfo {
  QString id, name;
};

class WeatherModels : public QObject {
  Q_OBJECT
public:
  static WeatherModels& instance();

  static QVector<WeatherModelInfo> available();
  static QString displayName(const QString& id);
  /** Points along a route every `spacingNm` (at most 40), incl. the ends. */
  static QVector<QPointF> samplePoints(const QVector<QPointF>& route,
                                       double spacingNm = 8);

  using Callback = std::function<void(const QHash<QString, ModelForecast>&,
                                      const QString& error)>;
  /** Fetches hourly wind of the models for the points (7 days). */
  void fetch(const QVector<QPointF>& points, const QStringList& models,
             Callback done);
  /** Parses an Open-Meteo answer (one object or an array per point). */
  static QHash<QString, ModelForecast> parse(const QByteArray& json,
                                             const QVector<QPointF>& points,
                                             const QStringList& models,
                                             QString* error = nullptr);

private:
  WeatherModels() = default;
  QNetworkAccessManager m_net;
};
