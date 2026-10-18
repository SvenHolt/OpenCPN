#include "core/BoatConfig.h"

#include <QSettings>

double SailConfig::areaFactor() const {
  static const double mainArea[] = {0.0, 0.45, 0.36, 0.28, 0.2};
  static const double headArea[] = {0.0, 0.55, 0.4, 0.15};
  static const double downArea[] = {0.0, 0.7, 0.9, 0.45};
  double a = mainArea[int(main)];
  a += headArea[int(head)] * (head == Head::Genoa ? headFurlPct / 100.0 : 1.0);
  a += downArea[int(downwind)];
  return a;
}

QString SailConfig::name(Main m) {
  switch (m) {
    case Main::Down:
      return QObject::tr("Furled");
    case Main::Full:
      return QObject::tr("Full");
    case Main::Reef1:
      return QObject::tr("1st reef");
    case Main::Reef2:
      return QObject::tr("2nd reef");
    case Main::Reef3:
      return QObject::tr("3rd reef");
  }
  return {};
}

QString SailConfig::name(Head h) {
  switch (h) {
    case Head::None:
      return QObject::tr("None");
    case Head::Genoa:
      return QObject::tr("Genoa");
    case Head::Jib:
      return QObject::tr("Jib");
    case Head::Storm:
      return QObject::tr("Storm jib");
  }
  return {};
}

QString SailConfig::name(Downwind d) {
  switch (d) {
    case Downwind::None:
      return QObject::tr("None");
    case Downwind::Gennaker:
      return QObject::tr("Gennaker");
    case Downwind::Spinnaker:
      return QObject::tr("Spinnaker");
    case Downwind::Code0:
      return QObject::tr("Code 0");
  }
  return {};
}

QString SailConfig::name(Propulsion p) {
  switch (p) {
    case Propulsion::Sailing:
      return QObject::tr("Sailing");
    case Propulsion::MotorSailing:
      return QObject::tr("Motor sailing");
    case Propulsion::Motor:
      return QObject::tr("Motor");
  }
  return {};
}

QString SailConfig::summary() const {
  QStringList parts;
  if (main != Main::Down)
    parts << QObject::tr("Main %1").arg(name(main).toLower());
  if (head != Head::None)
    parts << (head == Head::Genoa && headFurlPct < 100
                  ? QStringLiteral("%1 %2%").arg(name(head)).arg(headFurlPct)
                  : name(head));
  if (downwind != Downwind::None) parts << name(downwind);
  if (motoring()) parts << QObject::tr("Engine");
  return parts.isEmpty() ? QObject::tr("No sails set") : parts.join(" · ");
}

BoatConfig& BoatConfig::instance() {
  static BoatConfig c;
  return c;
}

void BoatConfig::setSails(const SailConfig& s) {
  m_sails = s;
  save();
  emit changed();
}

void BoatConfig::setEngineSetup(double tank, double consumption, double cruise,
                                double manualFuel) {
  tankLiters = tank;
  consumptionLph = consumption;
  cruiseRpm = cruise;
  manualFuelPct = manualFuel;
  save();
  emit changed();
}

void BoatConfig::load() {
  QSettings s;
  s.beginGroup("Boat");
  m_sails.propulsion = SailConfig::Propulsion(
      s.value("propulsion", int(m_sails.propulsion)).toInt());
  m_sails.main = SailConfig::Main(s.value("main", int(m_sails.main)).toInt());
  m_sails.head = SailConfig::Head(s.value("head", int(m_sails.head)).toInt());
  m_sails.headFurlPct = s.value("headFurlPct", m_sails.headFurlPct).toInt();
  m_sails.downwind =
      SailConfig::Downwind(s.value("downwind", int(m_sails.downwind)).toInt());
  tankLiters = s.value("tankLiters", tankLiters).toDouble();
  consumptionLph = s.value("consumptionLph", consumptionLph).toDouble();
  cruiseRpm = s.value("cruiseRpm", cruiseRpm).toDouble();
  manualFuelPct = s.value("manualFuelPct", manualFuelPct).toDouble();
  s.endGroup();
  emit changed();
}

void BoatConfig::save() const {
  QSettings s;
  s.beginGroup("Boat");
  s.setValue("propulsion", int(m_sails.propulsion));
  s.setValue("main", int(m_sails.main));
  s.setValue("head", int(m_sails.head));
  s.setValue("headFurlPct", m_sails.headFurlPct);
  s.setValue("downwind", int(m_sails.downwind));
  s.setValue("tankLiters", tankLiters);
  s.setValue("consumptionLph", consumptionLph);
  s.setValue("cruiseRpm", cruiseRpm);
  s.setValue("manualFuelPct", manualFuelPct);
  s.endGroup();
}
