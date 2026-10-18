// Boat polar: speed through the water over true wind speed and angle.
//
// Reads the common text formats:
//  * grid (OpenCPN, qtVlm, ORC/"TWA\TWS" tables, CSV): first row the wind
//    speeds, first column the wind angles; tab, semicolon, comma or blank
//    separated
//  * Expedition: one line per wind speed, "TWS angle speed angle speed …"
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class Polar {
public:
  QString name;
  QVector<double> tws;             // ascending, kn
  QVector<double> twa;             // ascending, 0..180°
  QVector<QVector<double>> speed;  // [twa][tws], kn; 0 = no data

  bool isValid() const { return tws.size() >= 2 && twa.size() >= 2; }
  bool load(const QString& path, QString* error = nullptr);
  bool parse(const QString& text, QString* error = nullptr);
  bool save(const QString& path) const;
  /** Grid format with tabs ("TWA\TWS" header). */
  QString toText() const;

  /** Bilinear interpolation; below the first wind speed the speed goes
   *  linearly to 0, above the last it stays constant. */
  double speedAt(double windKn, double twaDeg) const;
  /** Angle with the best velocity made good against / with the wind. */
  double bestUpwindTwa(double windKn) const;
  double bestDownwindTwa(double windKn) const;
  double maxSpeed() const;

  /** A generic cruising yacht polar scaled to a top speed. */
  static Polar generic(double topSpeedKn);
};

/** The boat's polar, stored with the boat setup. */
class PolarStore : public QObject {
  Q_OBJECT
public:
  static PolarStore& instance();
  const Polar& polar() const { return m_polar; }
  /** True if the skipper imported or edited a polar. */
  bool isCustom() const { return m_custom; }
  void setPolar(const Polar& p);
  /** Back to the generic polar for the given top speed. */
  void reset(double topSpeedKn);

signals:
  void changed();

private:
  PolarStore();
  QString path() const;
  Polar m_polar;
  bool m_custom = false;
};
