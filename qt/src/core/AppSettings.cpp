#include "core/AppSettings.h"

#include <QSettings>
#include <cmath>

AppSettings& AppSettings::instance() {
  static AppSettings s;
  return s;
}

void AppSettings::load() {
  QSettings s;
  s.beginGroup("Preferences");
  theme = ThemeMode(s.value("theme", int(theme)).toInt());
  speedUnit = SpeedUnit(s.value("speedUnit", int(speedUnit)).toInt());
  distanceUnit =
      DistanceUnit(s.value("distanceUnit", int(distanceUnit)).toInt());
  showGraticule = s.value("showGraticule", showGraticule).toBool();
  showAisNames = s.value("showAisNames", showAisNames).toBool();
  courseUp = s.value("courseUp", courseUp).toBool();
  predictorMinutes = s.value("predictorMinutes", predictorMinutes).toInt();
  cpaAlarmNm = s.value("cpaAlarmNm", cpaAlarmNm).toDouble();
  tcpaAlarmMin = s.value("tcpaAlarmMin", tcpaAlarmMin).toDouble();
  arrivalRadiusNm = s.value("arrivalRadiusNm", arrivalRadiusNm).toDouble();
  s.endGroup();
}

void AppSettings::save() {
  QSettings s;
  s.beginGroup("Preferences");
  s.setValue("theme", int(theme));
  s.setValue("speedUnit", int(speedUnit));
  s.setValue("distanceUnit", int(distanceUnit));
  s.setValue("showGraticule", showGraticule);
  s.setValue("showAisNames", showAisNames);
  s.setValue("courseUp", courseUp);
  s.setValue("predictorMinutes", predictorMinutes);
  s.setValue("cpaAlarmNm", cpaAlarmNm);
  s.setValue("tcpaAlarmMin", tcpaAlarmMin);
  s.setValue("arrivalRadiusNm", arrivalRadiusNm);
  s.endGroup();
}

void AppSettings::notifyChanged() {
  save();
  emit changed();
}

QPair<QString, QString> AppSettings::speed(double kn, int decimals) const {
  if (std::isnan(kn)) return {QStringLiteral("–"), {}};
  switch (speedUnit) {
    case SpeedUnit::KmH:
      return {QString::number(kn * 1.852, 'f', decimals), "km/h"};
    case SpeedUnit::Ms:
      return {QString::number(kn * 0.514444, 'f', decimals), "m/s"};
    default:
      return {QString::number(kn, 'f', decimals), "kn"};
  }
}

QPair<QString, QString> AppSettings::distance(double nm) const {
  if (std::isnan(nm)) return {QStringLiteral("–"), {}};
  if (distanceUnit == DistanceUnit::Kilometers) {
    const double km = nm * 1.852;
    if (km < 1) return {QString::number(km * 1000, 'f', 0), "m"};
    return {QString::number(km, 'f', km < 10 ? 2 : 1), "km"};
  }
  if (nm < 0.1) return {QString::number(nm * 1852, 'f', 0), "m"};
  return {QString::number(nm, 'f', nm < 10 ? 2 : 1), "NM"};
}

QString AppSettings::speedText(double kn) const {
  auto p = speed(kn);
  return p.second.isEmpty() ? p.first : p.first + ' ' + p.second;
}

QString AppSettings::distanceText(double nm) const {
  auto p = distance(nm);
  return p.second.isEmpty() ? p.first : p.first + ' ' + p.second;
}
