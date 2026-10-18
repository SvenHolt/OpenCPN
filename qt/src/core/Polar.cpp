#include "core/Polar.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Polar", s); }

QStringList tokens(QString line) {
  line = line.trimmed();
  if (line.contains(';')) {
    // European CSV: semicolons separate, commas are decimal points.
    QStringList t = line.split(';');
    for (QString& s : t) s = s.trimmed().replace(',', '.');
    return t;
  }
  if (line.contains('\t')) {
    QStringList t = line.split('\t');
    for (QString& s : t) s = s.trimmed();
    return t;
  }
  return line.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
}

bool number(const QString& s, double* v) {
  bool ok = false;
  const double d = s.toDouble(&ok);
  if (ok && v) *v = d;
  return ok;
}

/** Linear interpolation in a sorted table; clamped at the ends. */
double interp(const QVector<double>& x, const QVector<double>& y, double v) {
  if (x.isEmpty()) return 0;
  if (v <= x.first()) return y.first();
  if (v >= x.last()) return y.last();
  const auto it = std::upper_bound(x.begin(), x.end(), v);
  const int i = int(it - x.begin());
  const double f = (v - x[i - 1]) / (x[i] - x[i - 1]);
  return y[i - 1] + f * (y[i] - y[i - 1]);
}
}  // namespace

bool Polar::load(const QString& path, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error) *error = f.errorString();
    return false;
  }
  if (!parse(QString::fromUtf8(f.readAll()), error)) return false;
  name = QFileInfo(path).completeBaseName();
  return true;
}

bool Polar::parse(const QString& text, QString* error) {
  QVector<QStringList> rows;
  for (const QString& raw : text.split('\n')) {
    const QString l = raw.trimmed();
    if (l.isEmpty() || l.startsWith('#') || l.startsWith('!')) continue;
    rows << tokens(l);
  }
  const auto fail = [error](const QString& msg) {
    if (error) *error = msg;
    return false;
  };
  if (rows.size() < 2) return fail(tr("Not a polar table: too few lines."));

  Polar p;
  p.name = name;
  double first = 0;
  const bool header = !number(rows[0].value(0), &first);

  // Expedition: "tws a1 s1 a2 s2 …", every line numeric with odd length.
  bool expedition = !header;
  for (const QStringList& r : rows)
    expedition &= r.size() >= 3 && r.size() % 2 == 1;
  if (expedition) {
    // The first line of a grid with a numeric corner cell has the wind
    // speeds increasing by several knots; Expedition lines start with one
    // wind speed and have angle/speed pairs (angles increasing).
    const QStringList& r = rows[0];
    double a1 = 0, a2 = 0;
    if (r.size() >= 5 && number(r[1], &a1) && number(r[3], &a2))
      expedition = a2 > a1 && a1 <= 180 && a2 <= 180;
  }

  if (expedition) {
    std::map<double, std::map<double, double>> byTws;  // tws -> twa -> bsp
    QVector<double> angles;
    for (const QStringList& r : rows) {
      double w = 0;
      number(r[0], &w);
      for (int i = 1; i + 1 < r.size(); i += 2) {
        double a = 0, s = 0;
        if (!number(r[i], &a) || !number(r[i + 1], &s))
          return fail(tr("Invalid number in line \"%1\".").arg(r.join(' ')));
        byTws[w][a] = s;
        if (!angles.contains(a)) angles << a;
      }
    }
    std::sort(angles.begin(), angles.end());
    for (const auto& [w, m] : byTws) p.tws << w;
    p.twa = angles;
    p.speed.fill(QVector<double>(p.tws.size(), 0), p.twa.size());
    int c = 0;
    for (const auto& [w, m] : byTws) {
      QVector<double> xa, ys;
      for (const auto& [a, s] : m) {
        xa << a;
        ys << s;
      }
      for (int i = 0; i < p.twa.size(); ++i) {
        const double a = p.twa[i];
        // Angles outside a wind speed's own range have no speed.
        p.speed[i][c] =
            (a < xa.first() || a > xa.last()) ? 0 : interp(xa, ys, a);
      }
      ++c;
    }
  } else {
    // Grid: header row with wind speeds (first cell is the corner label).
    for (int i = 1; i < rows[0].size(); ++i) {
      double w = 0;
      if (!number(rows[0][i], &w))
        return fail(
            tr("Invalid wind speed \"%1\" in the first line.").arg(rows[0][i]));
      p.tws << w;
    }
    for (int r = 1; r < rows.size(); ++r) {
      double a = 0;
      if (!number(rows[r].value(0), &a))
        return fail(tr("Invalid wind angle \"%1\".").arg(rows[r].value(0)));
      QVector<double> line;
      for (int c = 0; c < p.tws.size(); ++c) {
        double s = 0;
        const QString cell = rows[r].value(c + 1);
        if (!cell.isEmpty() && !number(cell, &s))
          return fail(tr("Invalid boat speed \"%1\".").arg(cell));
        line << s;
      }
      p.twa << a;
      p.speed << line;
    }
  }
  // Sort and check.
  for (int i = 1; i < p.tws.size(); ++i)
    if (p.tws[i] <= p.tws[i - 1])
      return fail(tr("Wind speeds must increase from left to right."));
  QVector<int> order(p.twa.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(),
            [&p](int x, int y) { return p.twa[x] < p.twa[y]; });
  Polar sorted = p;
  for (int i = 0; i < order.size(); ++i) {
    sorted.twa[i] = p.twa[order[i]];
    sorted.speed[i] = p.speed[order[i]];
  }
  for (double a : sorted.twa)
    if (a < 0 || a > 180) return fail(tr("Wind angles must be 0–180°."));
  if (!sorted.isValid())
    return fail(tr("A polar needs at least two wind speeds and angles."));
  if (sorted.maxSpeed() <= 0) return fail(tr("The polar has no boat speeds."));
  *this = sorted;
  return true;
}

QString Polar::toText() const {
  QString out = "TWA\\TWS";
  for (double w : tws) out += '\t' + QString::number(w);
  out += '\n';
  for (int i = 0; i < twa.size(); ++i) {
    out += QString::number(twa[i]);
    for (double s : speed[i]) out += '\t' + QString::number(s, 'f', 2);
    out += '\n';
  }
  return out;
}

bool Polar::save(const QString& path) const {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
  f.write(toText().toUtf8());
  return f.commit();
}

double Polar::speedAt(double windKn, double twaDeg) const {
  if (!isValid() || windKn <= 0) return 0;
  double a = std::fabs(std::fmod(twaDeg, 360.0));
  if (a > 180) a = 360 - a;
  // Speed per wind angle row at this wind speed.
  QVector<double> col(twa.size());
  for (int i = 0; i < twa.size(); ++i) {
    if (windKn < tws.first()) {
      col[i] = speed[i].first() * windKn / tws.first();
    } else {
      col[i] = interp(tws, speed[i], windKn);
    }
  }
  if (a < twa.first() || a > twa.last()) return 0;
  return std::max(0.0, interp(twa, col, a));
}

double Polar::bestUpwindTwa(double windKn) const {
  double best = 45, bestVmg = 0;
  for (double a = 25; a <= 90; a += 0.5) {
    const double vmg = speedAt(windKn, a) * std::cos(a * M_PI / 180);
    if (vmg > bestVmg) {
      bestVmg = vmg;
      best = a;
    }
  }
  return best;
}

double Polar::bestDownwindTwa(double windKn) const {
  double best = 160, bestVmg = 0;
  for (double a = 90; a <= 180; a += 0.5) {
    const double vmg = -speedAt(windKn, a) * std::cos(a * M_PI / 180);
    if (vmg > bestVmg) {
      bestVmg = vmg;
      best = a;
    }
  }
  return best;
}

double Polar::maxSpeed() const {
  double m = 0;
  for (const auto& row : speed)
    for (double s : row) m = std::max(m, s);
  return m;
}

Polar Polar::generic(double topSpeedKn) {
  // Relative speed over the wind angle (1 = beam reach).
  static const double factor[][2] = {
      {0, 0},      {32, 0},     {36, 0.55},  {40, 0.66},  {45, 0.74},
      {52, 0.83},  {60, 0.9},   {75, 0.97},  {90, 1.0},   {110, 1.0},
      {120, 0.97}, {135, 0.92}, {150, 0.85}, {165, 0.76}, {180, 0.7}};
  Polar p;
  p.name = QCoreApplication::translate("Polar", "Generic cruiser");
  p.tws = {4, 6, 8, 10, 12, 14, 16, 20, 25, 30};
  for (const auto& f : factor) p.twa << f[0];
  for (const auto& f : factor) {
    QVector<double> row;
    for (double w : p.tws) {
      const double v = topSpeedKn * (1 - std::exp(-w / 8.0)) * 1.08 * f[1];
      row << std::round(std::min(v, topSpeedKn * 1.1) * 100) / 100;
    }
    p.speed << row;
  }
  return p;
}

// --------------------------------------------------------------------------

PolarStore& PolarStore::instance() {
  static PolarStore s;
  return s;
}

PolarStore::PolarStore() {
  QSettings s;
  m_custom = s.value("Polar/custom", false).toBool();
  if (m_custom && m_polar.load(path())) {
    m_polar.name = s.value("Polar/name", m_polar.name).toString();
  } else {
    m_custom = false;
    m_polar = Polar::generic(s.value("Polar/topSpeed", 7.0).toDouble());
  }
}

QString PolarStore::path() const {
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + "/boat.pol";
}

void PolarStore::setPolar(const Polar& p) {
  m_polar = p;
  m_custom = true;
  p.save(path());
  QSettings s;
  s.setValue("Polar/custom", true);
  s.setValue("Polar/name", p.name);
  emit changed();
}

void PolarStore::reset(double topSpeedKn) {
  m_polar = Polar::generic(topSpeedKn);
  m_custom = false;
  QSettings s;
  s.setValue("Polar/custom", false);
  s.setValue("Polar/topSpeed", topSpeedKn);
  emit changed();
}
