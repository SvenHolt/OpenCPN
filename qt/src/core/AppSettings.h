// User preferences of the Qt6 frontend, persisted with QSettings.
#pragma once

#include <QObject>
#include <QPair>
#include <QString>

class AppSettings : public QObject {
  Q_OBJECT
public:
  enum class ThemeMode { Night, Day, Auto };
  enum class SpeedUnit { Knots, KmH, Ms };
  enum class DistanceUnit { NauticalMiles, Kilometers };

  static AppSettings& instance();

  ThemeMode theme = ThemeMode::Night;
  SpeedUnit speedUnit = SpeedUnit::Knots;
  DistanceUnit distanceUnit = DistanceUnit::NauticalMiles;
  bool showGraticule = true;
  bool showAisNames = true;
  bool courseUp = false;
  int predictorMinutes = 6;
  double cpaAlarmNm = 0.5;
  double tcpaAlarmMin = 15;
  double arrivalRadiusNm = 0.05;
  // Chart depth shading (meters)
  double shallowDepth = 2;
  double safetyDepth = 5;
  double deepDepth = 20;
  bool showChartOutlines = true;
  // Track recording
  bool trackRecording = true;
  bool startInCockpit = true;

  void load();
  void save();
  /** Call after modifying fields so the UI refreshes. */
  void notifyChanged();

  /** Value/unit pairs formatted in the user's units. */
  QPair<QString, QString> speed(double knots, int decimals = 1) const;
  QPair<QString, QString> distance(double nm) const;
  QString speedText(double knots) const;
  QString distanceText(double nm) const;

signals:
  void changed();

private:
  AppSettings() = default;
};
