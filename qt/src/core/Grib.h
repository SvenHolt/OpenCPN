// GRIB edition 1 and 2 decoder for regular lat/lon grids.
//
// Supported packing: GRIB1 simple packing; GRIB2 templates 5.0 (simple),
// 5.2 (complex) and 5.3 (complex with spatial differencing), which covers
// NOAA GFS/NOMADS, Saildocs, ECMWF open data and most routing sources.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>
#include <memory>

struct GribField {
  enum class Param {
    WindU,
    WindV,
    Pressure,     // Pa at mean sea level
    Temperature,  // K
    Gust,         // m/s
    WaveHeight,   // m
    Precipitation,
    Other
  };
  Param param = Param::Other;
  QString name;  // e.g. "UGRD"
  int edition = 2;
  int discipline = 0, category = 0, number = 0;
  int levelType = 0;
  double levelValue = 0;
  QDateTime refTime, validTime;

  // Regular lat/lon grid; values[j * ni + i] at (lat1 + j*dlat, lon1 + i*dlon)
  int ni = 0, nj = 0;
  double lat1 = 0, lon1 = 0, dlat = 0, dlon = 0;
  QVector<float> values;  // NaN = missing

  bool isValid() const {
    return ni > 0 && nj > 0 && values.size() == qsizetype(ni) * nj;
  }
  /** Bilinear interpolation; NaN outside the grid. */
  double sample(double lat, double lon) const;
  bool contains(double lat, double lon) const;
};

/** One forecast time with the fields needed for display. */
struct GribTimeStep {
  QDateTime time;
  const GribField* windU = nullptr;
  const GribField* windV = nullptr;
  const GribField* pressure = nullptr;
  const GribField* gust = nullptr;
  const GribField* waves = nullptr;
};

class GribFile {
public:
  bool load(const QString& path, QString* error = nullptr);
  bool parse(const QByteArray& data, QString* error = nullptr);

  const QVector<GribField>& fields() const { return m_fields; }
  const QVector<GribTimeStep>& steps() const { return m_steps; }
  QString path() const { return m_path; }

  /** Wind (knots, direction from, degrees true) at a position/step. */
  bool wind(int step, double lat, double lon, double& speedKn,
            double& dirFrom) const;

  static bool decodeGrib1(const uchar* msg, qsizetype len, GribField& f,
                          QString* error);
  static bool decodeGrib2(const uchar* msg, qsizetype len,
                          QVector<GribField>& out, QString* error);

private:
  void buildSteps();
  QString m_path;
  QVector<GribField> m_fields;
  QVector<GribTimeStep> m_steps;
};

/** Application-wide GRIB state: loaded file and selected time step. */
class GribManager : public QObject {
  Q_OBJECT
public:
  static GribManager& instance();

  std::shared_ptr<const GribFile> file() const { return m_file; }
  bool open(const QString& path, QString* error = nullptr);
  void close();
  int step() const { return m_step; }
  void setStep(int step);
  /** Selects the time step closest to now. */
  void selectNow();
  const GribTimeStep* currentStep() const;

  bool showWind = true, showPressure = true, showHeatmap = true;
  void setOptions(bool wind, bool pressure, bool heatmap);

signals:
  void fileChanged();
  void stepChanged();
  void optionsChanged();

private:
  GribManager() = default;
  std::shared_ptr<const GribFile> m_file;
  int m_step = 0;
};
