#include "core/Nmea.h"

#include <QRegularExpression>
#include <QStringList>
#include <cmath>

static quint8 checksumOf(QStringView body) {
  quint8 c = 0;
  for (QChar ch : body) c ^= quint8(ch.unicode());
  return c;
}

bool NmeaParser::verifyChecksum(const QString& s) {
  const int star = s.lastIndexOf('*');
  if (s.size() < 2 || (s[0] != '$' && s[0] != '!')) return false;
  if (star < 0) return true;  // checksum is optional for many sentences
  bool ok = false;
  const int expected = s.mid(star + 1, 2).toInt(&ok, 16);
  if (!ok) return false;
  return checksumOf(QStringView(s).mid(1, star - 1)) == expected;
}

QString NmeaParser::makeSentence(const QString& body, QChar start) {
  return QStringLiteral("%1%2*%3").arg(start).arg(body).arg(
      QString::number(checksumOf(body), 16)
          .rightJustified(2, QChar('0'))
          .toUpper());
}

std::optional<double> NmeaParser::parseLatLon(const QString& v,
                                              const QString& hemi) {
  if (v.isEmpty() || hemi.isEmpty()) return std::nullopt;
  const int dot = v.indexOf('.');
  const int degLen = (dot < 0 ? v.size() : dot) - 2;
  if (degLen < 1) return std::nullopt;
  bool ok1 = false, ok2 = false;
  const double deg = v.left(degLen).toDouble(&ok1);
  const double min = v.mid(degLen).toDouble(&ok2);
  if (!ok1 || !ok2) return std::nullopt;
  double r = deg + min / 60.0;
  if (hemi == "S" || hemi == "W") r = -r;
  return r;
}

void NmeaParser::feed(const QByteArray& data) {
  m_buffer.append(data);
  int nl;
  while ((nl = m_buffer.indexOf('\n')) >= 0) {
    const QString line = QString::fromLatin1(m_buffer.left(nl)).trimmed();
    m_buffer.remove(0, nl + 1);
    if (!line.isEmpty()) parseLine(line);
  }
  if (m_buffer.size() > 8192) m_buffer.clear();  // garbage protection
}

static std::optional<double> optDouble(const QString& s) {
  bool ok = false;
  const double v = s.toDouble(&ok);
  if (!ok || s.isEmpty()) return std::nullopt;
  return v;
}

static std::optional<QDateTime> parseTime(const QString& hhmmss,
                                          const QString& ddmmyy) {
  if (hhmmss.size() < 6) return std::nullopt;
  QTime t(hhmmss.left(2).toInt(), hhmmss.mid(2, 2).toInt(),
          hhmmss.mid(4, 2).toInt());
  QDate d = QDate::currentDate();
  if (ddmmyy.size() == 6)
    d = QDate(2000 + ddmmyy.mid(4, 2).toInt(), ddmmyy.mid(2, 2).toInt(),
              ddmmyy.left(2).toInt());
  QDateTime dt(d, t, Qt::UTC);
  if (!dt.isValid()) return std::nullopt;
  return dt;
}

bool NmeaParser::parseLine(const QString& raw) {
  QString line = raw;
  const int start = line.indexOf(QRegularExpression("[$!]"));
  if (start < 0) return false;
  if (start > 0) line = line.mid(start);
  if (!verifyChecksum(line)) return false;
  emit sentenceReceived(line);

  const int star = line.lastIndexOf('*');
  const QString body = line.mid(1, star < 0 ? -1 : star - 1);
  const QStringList f = body.split(',');
  if (f.isEmpty() || f[0].size() < 5) return false;
  const QString type = f[0].right(3);

  if (type == "VDM" || type == "VDO") {
    if (auto m = m_ais.addFragment(f)) emit aisMessage(*m);
    return true;
  }

  NavUpdate u;
  if (type == "RMC" && f.size() >= 10) {
    if (f[2] != "A") {
      u.fixLost = true;
    } else {
      u.lat = parseLatLon(f[3], f[4]);
      u.lon = parseLatLon(f[5], f[6]);
      u.sog = optDouble(f[7]);
      u.cog = optDouble(f[8]);
      u.time = parseTime(f[1], f[9]);
    }
  } else if (type == "GGA" && f.size() >= 8) {
    if (f[6].toInt() == 0) {
      u.fixLost = true;
    } else {
      u.lat = parseLatLon(f[2], f[3]);
      u.lon = parseLatLon(f[4], f[5]);
      u.satellites = f[7].toInt();
    }
  } else if (type == "GLL" && f.size() >= 7) {
    if (f[6] == "A") {
      u.lat = parseLatLon(f[1], f[2]);
      u.lon = parseLatLon(f[3], f[4]);
    }
  } else if (type == "VTG" && f.size() >= 8) {
    u.cog = optDouble(f[1]);
    u.sog = optDouble(f[5]);
  } else if ((type == "HDT" || type == "THS") && f.size() >= 2) {
    u.hdg = optDouble(f[1]);
  } else if (type == "MWV" && f.size() >= 5) {
    // Wind relative to the bow: R = apparent, T = theoretical (true)
    if (f.size() >= 6 && f[5] != "A") return false;
    auto angle = optDouble(f[1]);
    auto speed = optDouble(f[3]);
    if (!angle || !speed) return false;
    double a = *angle > 180 ? *angle - 360 : *angle;
    double kn = *speed;
    if (f[4] == "K")
      kn /= 1.852;
    else if (f[4] == "M")
      kn *= 1.943844;
    else if (f[4] == "S")
      kn *= 0.868976;
    if (f[2] == "R") {
      u.awa = a;
      u.aws = kn;
    } else {
      u.twa = a;
      u.tws = kn;
    }
  } else if (type == "MWD" && f.size() >= 6) {
    u.twd = optDouble(f[1]);
    u.tws = optDouble(f[5]);
    if (!u.tws && f.size() >= 8) {
      if (auto ms = optDouble(f[7])) u.tws = *ms * 1.943844;
    }
  } else if (type == "VHW" && f.size() >= 6) {
    u.stw = optDouble(f[5]);
    if (!u.stw && f.size() >= 8)
      if (auto kmh = optDouble(f[7])) u.stw = *kmh / 1.852;
  } else if (type == "DPT" && f.size() >= 2) {
    u.depth = optDouble(f[1]);
    if (u.depth && f.size() >= 3)
      if (auto off = optDouble(f[2]); off && *off > 0) *u.depth += *off;
  } else if (type == "DBT" && f.size() >= 5) {
    u.depth = optDouble(f[3]);
  } else if (type == "RPM" && f.size() >= 4) {
    if (f[1] != "E") return false;  // shaft RPM is not engine RPM
    u.rpm = optDouble(f[3]);
  } else if (type == "XDR" && f.size() >= 5) {
    // Quadruples: type, value, unit, name
    for (int i = 1; i + 2 < f.size(); i += 4) {
      const QString t = f[i];
      const auto v = optDouble(f[i + 1]);
      const QString unit = f[i + 2];
      const QString name = i + 3 < f.size() ? f[i + 3].toUpper() : QString();
      if (!v) continue;
      if (t == "C" && (name.contains("ENG") || name.contains("COOL")))
        u.coolantC = unit == "K" ? *v - 273.15 : *v;
      else if (t == "P" && name.contains("OIL"))
        u.oilBar = unit == "P" ? *v / 1e5 : *v;
      else if (t == "V" && name.contains("FUEL"))
        u.fuelPct = *v;
      else if (t == "U" && (name.contains("BAT") || name.contains("ALT")))
        u.voltage = *v;
      else if (t == "G" && name.contains("HOUR"))
        u.engineHours = *v;
      else if (t == "T" && name.contains("ENG"))
        u.rpm = *v;
      else if (t == "E" && name.contains("FUELRATE"))
        u.fuelRateLph = *v;
      else if (t == "A" && (name.contains("ROLL") || name.contains("HEEL")))
        u.heel = unit == "R" ? *v * 180 / M_PI : *v;
    }
  } else {
    return false;
  }
  emit navUpdate(u);
  return true;
}
