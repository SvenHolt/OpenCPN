#include "core/Logbook.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>
#include <cmath>

#include "core/Legs.h"

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Logbook", s); }

QString compass(double deg) {
  static const char* names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return names[int(std::round(std::fmod(deg + 360, 360) / 45)) % 8];
}

QString hoursText(double h) {
  const int m = int(std::round(h * 60));
  return QStringLiteral("%1:%2 h").arg(m / 60).arg(m % 60, 2, 10, QChar('0'));
}
}  // namespace

QJsonObject LogEntry::toJson() const {
  return {{"date", date.toString(Qt::ISODate)},
          {"crew", crew},
          {"weather", weather},
          {"report", report},
          {"updated", updated.toString(Qt::ISODate)}};
}

LogEntry LogEntry::fromJson(const QJsonObject& o) {
  LogEntry e;
  e.date = QDate::fromString(o["date"].toString(), Qt::ISODate);
  e.crew = o["crew"].toString();
  e.weather = o["weather"].toString();
  e.report = o["report"].toString();
  e.updated = QDateTime::fromString(o["updated"].toString(), Qt::ISODate);
  return e;
}

// --------------------------------------------------------------------------

DayFacts DayFacts::of(const QDate& date) {
  DayFacts f;
  f.date = date;
  double twsW = 0, twsSum = 0, u = 0, v = 0;
  f.minTempC = 1e9;
  f.maxTempC = -1e9;
  for (const Leg* l : LegStore::instance().legsOn(date)) {
    ++f.legs;
    const LegSummary s = l->summary();
    f.distanceNm += s.distanceNm;
    f.hours += s.hours;
    f.engineHours += s.engineHours;
    f.fuelL += s.fuelL;
    f.maxSog = std::max(f.maxSog, s.maxSog);
    f.maxTws = std::max(f.maxTws, s.maxTws);
    f.maxHeel = std::max(f.maxHeel, s.maxHeel);
    twsSum += s.avgTws * s.hours;
    twsW += s.hours;
    if (s.maxTempC >= s.minTempC && s.hours > 0) {
      f.minTempC = std::min(f.minTempC, s.minTempC);
      f.maxTempC = std::max(f.maxTempC, s.maxTempC);
    }
    for (const auto& p : l->samples)
      if (!std::isnan(p.twd) && !std::isnan(p.tws)) {
        u += p.tws * std::sin(p.twd * M_PI / 180);
        v += p.tws * std::cos(p.twd * M_PI / 180);
      }
    const QString from = l->from.isEmpty() ? l->name : l->from;
    if (f.places.isEmpty() || f.places.last() != from) f.places << from;
    if (!l->to.isEmpty()) f.places << l->to;
  }
  if (twsW > 0) f.avgTws = twsSum / twsW;
  if (u != 0 || v != 0) f.windDir = compass(std::atan2(u, v) * 180 / M_PI);
  if (f.minTempC > f.maxTempC) f.minTempC = f.maxTempC = 0;
  return f;
}

QString DayFacts::text() const {
  if (legs == 0) return tr("No leg recorded – in harbour or at anchor.");
  QStringList parts;
  if (!places.isEmpty()) parts << places.join(" – ");
  parts << tr("%1 NM in %2").arg(distanceNm, 0, 'f', 1).arg(hoursText(hours));
  if (avgTws > 0)
    parts << tr("wind %1 %2 kn, max. %3 kn")
                 .arg(windDir)
                 .arg(avgTws, 0, 'f', 0)
                 .arg(maxTws, 0, 'f', 0);
  if (engineHours > 0.05)
    parts << tr("engine %1, %2 l")
                 .arg(hoursText(engineHours))
                 .arg(fuelL, 0, 'f', 1);
  if (maxTempC != 0 || minTempC != 0)
    parts << tr("%1–%2 °C").arg(minTempC, 0, 'f', 0).arg(maxTempC, 0, 'f', 0);
  return parts.join(" · ");
}

// --------------------------------------------------------------------------

Logbook& Logbook::instance() {
  static Logbook s;
  return s;
}

Logbook::Logbook() { load(); }

QString Logbook::file() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/logbook.json";
}

void Logbook::load() {
  m_entries.clear();
  QFile f(file());
  if (!f.open(QIODevice::ReadOnly)) return;
  for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
    const LogEntry e = LogEntry::fromJson(v.toObject());
    if (e.date.isValid()) m_entries.insert(e.date, e);
  }
}

void Logbook::reload() {
  load();
  emit changed();
}

void Logbook::store() const {
  QJsonArray arr;
  for (const auto& e : m_entries) arr.append(e.toJson());
  const QString path = file();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(QJsonDocument(arr).toJson());
  f.commit();
}

LogEntry Logbook::entry(const QDate& date) const {
  LogEntry e = m_entries.value(date);
  e.date = date;
  return e;
}

void Logbook::save(const LogEntry& e) {
  if (!e.date.isValid()) return;
  if (e.isEmpty()) {
    if (!m_entries.remove(e.date)) return;
  } else {
    LogEntry copy = e;
    copy.updated = QDateTime::currentDateTimeUtc();
    m_entries.insert(e.date, copy);
  }
  store();
  emit changed();
}

QVector<QDate> Logbook::days() const {
  QSet<QDate> set;
  for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
    set.insert(it.key());
  for (const Leg& l : LegStore::instance().legs())
    for (QDate d = l.start().toLocalTime().date();
         d <= l.end().toLocalTime().date(); d = d.addDays(1))
      set.insert(d);
  QVector<QDate> out(set.begin(), set.end());
  std::sort(out.begin(), out.end(), std::greater<QDate>());
  return out;
}

QString Logbook::markdown() const {
  QString md = "# " + tr("Logbook") + "\n";
  QVector<QDate> all = days();
  std::sort(all.begin(), all.end());
  for (const QDate& d : all) {
    const LogEntry e = entry(d);
    md += "\n## " + QLocale().toString(d, QLocale::LongFormat) + "\n\n";
    md += "*" + DayFacts::of(d).text() + "*\n";
    if (!e.crew.trimmed().isEmpty())
      md += "\n**" + tr("Crew") + ":** " + e.crew.trimmed() + "  \n";
    if (!e.weather.trimmed().isEmpty())
      md += (e.crew.trimmed().isEmpty() ? "\n" : "") + QString("**") +
            tr("Weather") + ":** " + e.weather.trimmed() + "\n";
    if (!e.report.trimmed().isEmpty()) md += "\n" + e.report.trimmed() + "\n";
  }
  return md;
}
