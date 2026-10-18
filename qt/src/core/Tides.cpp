#include "core/Tides.h"

#include <algorithm>
#include <QFile>
#include <QTextStream>
#include <QtConcurrent>
#include <cmath>

#include "core/GeoMath.h"

namespace {

/** Reads the next non-comment, non-empty line. */
bool nextLine(QTextStream& in, QString& line) {
  while (!in.atEnd()) {
    line = in.readLine();
    if (line.isEmpty() || line[0] == '#') continue;
    return true;
  }
  return false;
}

double hhmmToSecs(const QString& s) {
  // "01:00", "-3:30", "1:0"
  const QString t = s.trimmed().section(' ', 0, 0);
  const bool neg = t.startsWith('-');
  const QStringList p = t.mid(neg ? 1 : 0).split(':');
  const double secs = p.value(0).toInt() * 3600.0 + p.value(1).toInt() * 60.0;
  return neg ? -secs : secs;
}

}  // namespace

TideDatabase& TideDatabase::instance() {
  static TideDatabase db;
  return db;
}

const QVector<TideStation>& TideDatabase::stations() const {
  static const QVector<TideStation> empty;
  return m_data ? m_data->stations : empty;
}

std::shared_ptr<TideData> TideDatabase::load(const QString& harmonicsFile,
                                             const QString& indexFile,
                                             QString* error) {
  auto d = std::make_shared<TideData>();
  QFile hf(harmonicsFile);
  if (!hf.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error) *error = hf.errorString();
    return nullptr;
  }
  QTextStream in(&hf);
  QString line;
  auto fail = [&](const QString& why) -> std::shared_ptr<TideData> {
    if (error) *error = why;
    return nullptr;
  };

  // Constituents
  if (!nextLine(in, line)) return fail("empty harmonics file");
  const int ncst = line.trimmed().toInt();
  if (ncst <= 0 || ncst > 1000) return fail("bad constituent count");
  for (int i = 0; i < ncst; ++i) {
    if (!nextLine(in, line)) return fail("truncated constituents");
    const QStringList f = line.simplified().split(' ');
    d->cstNames << f.value(0);
    d->speeds << f.value(1).toDouble() * M_PI / 648000.0;  // deg/h -> rad/s
  }
  nextLine(in, line);
  d->firstYear = line.trimmed().toInt();
  nextLine(in, line);
  d->numYears = line.trimmed().toInt();
  if (d->numYears <= 0 || d->numYears > 1000) return fail("bad year count");

  // The epoch and node tables are whitespace separated: name, then values.
  auto readTable = [&](QVector<QVector<double>>& table, double factor,
                       int count) {
    table.resize(ncst);
    for (int c = 0; c < ncst; ++c) {
      QString name;
      in >> name;
      table[c].resize(count);
      for (int y = 0; y < count; ++y) {
        double v;
        in >> v;
        table[c][y] = v * factor;
      }
    }
    QString end;
    in >> end;  // *END*
    return end == "*END*";
  };
  if (!readTable(d->equilibrium, M_PI / 180.0, d->numYears))
    return fail("bad equilibrium table");
  in.readLine();
  nextLine(in, line);
  const int numNodes = line.trimmed().toInt();
  if (!readTable(d->nodes, 1.0, numNodes)) return fail("bad node table");
  in.readLine();

  // Stations
  QHash<QString, int> byName;
  while (nextLine(in, line)) {
    HarmonicStation h;
    h.name = line.trimmed();
    QString mer, datum;
    if (!nextLine(in, mer) || !nextLine(in, datum)) break;
    h.meridianSecs = hhmmToSecs(mer);
    const QStringList df = datum.simplified().split(' ');
    h.datum = df.value(0).toDouble();
    const QString unit = df.value(1).toLower();
    if (unit.startsWith("knot")) {
      h.unit = "kn";
    } else {
      h.unit = "m";
      if (unit.startsWith("feet") || unit.startsWith("ft")) h.toMeters = 0.3048;
    }
    for (int c = 0; c < ncst; ++c) {
      if (!nextLine(in, line)) break;
      const QStringList f = line.simplified().split(' ');
      const double amp = f.value(1).toDouble();
      if (amp == 0) continue;
      h.csts << c;
      h.amp << amp;
      h.kappa << f.value(2).toDouble() * M_PI / 180.0;
    }
    byName.insert(h.name, int(d->harmonics.size()));
    d->harmonics << h;
  }

  // Index: positions and subordinate offsets.
  QFile xf(indexFile);
  if (!xf.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error) *error = xf.errorString();
    return nullptr;
  }
  QTextStream ix(&xf);
  TideStation* pending = nullptr;
  while (!ix.atEnd()) {
    const QString l = ix.readLine();
    if (l.isEmpty()) continue;
    const QChar k = l[0];
    if (k == '&' && pending) {
      // &Hmin Hmpy Hoff Lmin Lmpy Loff StaID RefFileNum RefName
      const QStringList f = l.mid(1).simplified().split(' ');
      if (f.size() < 9) continue;
      pending->highTimeOffMin = f[0].toDouble();
      pending->highMpy = f[1].toDouble() == 0 ? 1 : f[1].toDouble();
      pending->highOff = f[2].toDouble();
      pending->lowTimeOffMin = f[3].toDouble();
      pending->lowMpy = f[4].toDouble() == 0 ? 1 : f[4].toDouble();
      pending->lowOff = f[5].toDouble();
      const QString ref = f.mid(8).join(' ');
      pending->harmonic = byName.value(ref, -1);
      if (pending->harmonic < 0) d->stations.removeLast();
      pending = nullptr;
      continue;
    }
    if (k != 'T' && k != 'C' && k != 't' && k != 'c') continue;
    // "TEur:Ger:: 8.717 53.867 1:0 Cuxhaven, Steubenhoft, Germany"
    const QStringList f = l.simplified().split(' ');
    if (f.size() < 5) continue;
    TideStation s;
    s.type = (k == 'C' || k == 'c') ? TideStation::Type::Current
                                    : TideStation::Type::Tide;
    s.reference = k.isUpper();
    s.lon = f[1].toDouble();
    s.lat = f[2].toDouble();
    s.name = f.mid(4).join(' ');
    if (s.reference) {
      s.harmonic = byName.value(s.name, -1);
      if (s.harmonic < 0) continue;
      d->stations << s;
    } else {
      d->stations << s;
      pending = &d->stations.last();
    }
  }
  return d;
}

void TideDatabase::loadAsync(const QString& dir) {
  connect(&m_watcher, &QFutureWatcher<std::shared_ptr<TideData>>::finished,
          this, [this] {
            if (auto d = m_watcher.result()) setData(d);
          });
  m_watcher.setFuture(QtConcurrent::run([dir] {
    QString err;
    auto d = load(dir + "/HARMONICS_NO_US", dir + "/HARMONICS_NO_US.IDX", &err);
    if (!d) qWarning("Tide data: %s", qPrintable(err));
    return d;
  }));
}

void TideDatabase::setData(std::shared_ptr<const TideData> data) {
  m_data = std::move(data);
  emit loaded();
}

double TideDatabase::value(const TideData& d, const TideStation& s,
                           const QDateTime& utcIn) {
  if (s.harmonic < 0) return qQNaN();
  const HarmonicStation& h = d.harmonics[s.harmonic];
  // Subordinate stations: shift time by the mean of the HW/LW offsets.
  const QDateTime utc =
      utcIn.addSecs(-qint64((s.highTimeOffMin + s.lowTimeOffMin) / 2 * 60));
  const int year = utc.toUTC().date().year();
  const int yi = qBound(0, year - d.firstYear, d.numYears - 1);
  const qint64 epoch =
      QDateTime(QDate(year, 1, 1), QTime(0, 0), Qt::UTC).toSecsSinceEpoch();
  const double t = double(utc.toSecsSinceEpoch() - epoch) + h.meridianSecs;
  double sum = 0;
  for (int i = 0; i < h.csts.size(); ++i) {
    const int c = h.csts[i];
    const double node = yi < d.nodes[c].size() ? d.nodes[c][yi] : 1.0;
    sum += h.amp[i] * node *
           std::cos(d.speeds[c] * t + d.equilibrium[c][yi] - h.kappa[i]);
  }
  double v = h.datum + sum;
  if (!s.reference) {
    const double mpy = (s.highMpy + s.lowMpy) / 2;
    const double off = (s.highOff + s.lowOff) / 2;
    v = v * mpy + off;
  }
  return v * h.toMeters;
}

double TideDatabase::value(const TideStation& s, const QDateTime& utc) const {
  return m_data ? value(*m_data, s, utc) : qQNaN();
}

QString TideDatabase::unit(const TideStation& s) const {
  if (!m_data || s.harmonic < 0) return {};
  return m_data->harmonics[s.harmonic].unit;
}

QVector<TideExtreme> TideDatabase::extremes(const TideStation& s,
                                            const QDateTime& from,
                                            const QDateTime& to) const {
  QVector<TideExtreme> out;
  if (!m_data) return out;
  // Sample every 6 minutes, refine each turning point to one minute.
  const qint64 step = 360;
  QDateTime t = from;
  double prev = value(s, t);
  double prevSlope = qQNaN();
  while (t < to) {
    const QDateTime n = t.addSecs(step);
    const double v = value(s, n);
    const double slope = v - prev;
    if (!std::isnan(prevSlope) && (prevSlope > 0) != (slope > 0)) {
      // Turning point between t - step and n: refine.
      QDateTime best = t;
      double bestV = prev;
      for (qint64 dt = -step; dt <= step; dt += 60) {
        const QDateTime c = t.addSecs(dt);
        const double cv = value(s, c);
        if ((prevSlope > 0 && cv > bestV) || (prevSlope <= 0 && cv < bestV)) {
          bestV = cv;
          best = c;
        }
      }
      out << TideExtreme{best, bestV, prevSlope > 0};
    }
    prevSlope = slope;
    prev = v;
    t = n;
  }
  return out;
}

QVector<const TideStation*> TideDatabase::nearest(
    double lat, double lon, int count, TideStation::Type type) const {
  QVector<QPair<double, const TideStation*>> all;
  for (const auto& s : stations())
    if (s.type == type)
      all.push_back({geo::distanceNm(lat, lon, s.lat, s.lon), &s});
  std::partial_sort(
      all.begin(), all.begin() + std::min<qsizetype>(count, all.size()),
      all.end(),
      [](const auto& a, const auto& b) { return a.first < b.first; });
  QVector<const TideStation*> out;
  for (int i = 0; i < std::min<qsizetype>(count, all.size()); ++i)
    out << all[i].second;
  return out;
}
