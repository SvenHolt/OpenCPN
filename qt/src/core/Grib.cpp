#include "core/Grib.h"

#include <algorithm>
#include <QFile>
#include <QMap>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();

quint32 u16(const uchar* p) { return quint32(p[0]) << 8 | p[1]; }
quint32 u24(const uchar* p) { return quint32(p[0]) << 16 | u16(p + 1); }
quint32 u32(const uchar* p) { return quint32(p[0]) << 24 | u24(p + 1); }
quint64 u64(const uchar* p) { return quint64(u32(p)) << 32 | u32(p + 4); }
// GRIB stores signed integers as sign and magnitude.
qint32 s16(const uchar* p) {
  const qint32 v = qint32(u16(p) & 0x7fff);
  return (p[0] & 0x80) ? -v : v;
}
qint32 s24(const uchar* p) {
  const qint32 v = qint32(u24(p) & 0x7fffff);
  return (p[0] & 0x80) ? -v : v;
}
qint32 s32(const uchar* p) {
  const qint32 v = qint32(u32(p) & 0x7fffffff);
  return (p[0] & 0x80) ? -v : v;
}
float ieee32(const uchar* p) {
  const quint32 v = u32(p);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
double ibm32(const uchar* p) {
  const int sign = (p[0] & 0x80) ? -1 : 1;
  const int exp = (p[0] & 0x7f) - 64;
  const double mant = double(u24(p + 1)) / double(1 << 24);
  return sign * mant * std::pow(16.0, exp);
}

class BitReader {
public:
  BitReader(const uchar* data, qsizetype bytes) : d(data), n(bytes * 8) {}
  quint32 get(int bits) {
    quint32 v = 0;
    for (int i = 0; i < bits; ++i) {
      v <<= 1;
      if (pos < n) v |= (d[pos >> 3] >> (7 - (pos & 7))) & 1;
      ++pos;
    }
    return v;
  }
  void align() { pos = (pos + 7) & ~qint64(7); }
  qint64 bitPos() const { return pos; }
  bool overrun() const { return pos > n; }

private:
  const uchar* d;
  qint64 n;
  qint64 pos = 0;
};

QDateTime addForecast(const QDateTime& ref, int unit, qint64 value) {
  switch (unit) {
    case 0:
      return ref.addSecs(value * 60);
    case 1:
      return ref.addSecs(value * 3600);
    case 2:
      return ref.addDays(value);
    case 10:
      return ref.addSecs(value * 3 * 3600);
    case 11:
      return ref.addSecs(value * 6 * 3600);
    case 12:
      return ref.addSecs(value * 12 * 3600);
    case 13:
    case 254:
      return ref.addSecs(value);
    default:
      return ref.addSecs(value * 3600);
  }
}

void classify(GribField& f) {
  using P = GribField::Param;
  if (f.edition == 1) {
    switch (f.number) {
      case 2:
        f.param = P::Pressure, f.name = "PRMSL";
        break;
      case 11:
        f.param = P::Temperature, f.name = "TMP";
        break;
      case 33:
        f.param = P::WindU, f.name = "UGRD";
        break;
      case 34:
        f.param = P::WindV, f.name = "VGRD";
        break;
      case 61:
        f.param = P::Precipitation, f.name = "APCP";
        break;
      case 100:
        f.param = P::WaveHeight, f.name = "HTSGW";
        break;
      case 180:
        f.param = P::Gust, f.name = "GUST";
        break;
      default:
        f.name = QStringLiteral("P%1").arg(f.number);
    }
    return;
  }
  const int d = f.discipline, c = f.category, n = f.number;
  if (d == 0 && c == 2 && n == 2)
    f.param = P::WindU, f.name = "UGRD";
  else if (d == 0 && c == 2 && n == 3)
    f.param = P::WindV, f.name = "VGRD";
  else if (d == 0 && c == 2 && n == 22)
    f.param = P::Gust, f.name = "GUST";
  else if (d == 0 && c == 3 && (n == 1 || n == 0))
    f.param = P::Pressure, f.name = n == 1 ? "PRMSL" : "PRES";
  else if (d == 0 && c == 0 && n == 0)
    f.param = P::Temperature, f.name = "TMP";
  else if (d == 0 && c == 1 && (n == 8 || n == 7))
    f.param = P::Precipitation, f.name = n == 8 ? "APCP" : "PRATE";
  else if (d == 10 && c == 0 && n == 3)
    f.param = P::WaveHeight, f.name = "HTSGW";
  else
    f.name = QStringLiteral("%1.%2.%3").arg(d).arg(c).arg(n);
}

/** Expands packed values over the bitmap into the full grid. */
void applyBitmap(GribField& f, const QVector<float>& packed,
                 const uchar* bitmap) {
  const qsizetype total = qsizetype(f.ni) * f.nj;
  f.values.resize(total);
  if (!bitmap) {
    for (qsizetype i = 0; i < total; ++i)
      f.values[i] = i < packed.size() ? packed[i] : kMissing;
    return;
  }
  qsizetype k = 0;
  for (qsizetype i = 0; i < total; ++i) {
    const bool present = (bitmap[i >> 3] >> (7 - (i & 7))) & 1;
    f.values[i] = present && k < packed.size() ? packed[k++] : kMissing;
  }
}

}  // namespace

// --------------------------------------------------------------------------

bool GribField::contains(double lat, double lon) const {
  return !std::isnan(sample(lat, lon));
}

double GribField::sample(double lat, double lon) const {
  if (!isValid() || dlat == 0 || dlon == 0) return qQNaN();
  double fi = (lon - lon1) / dlon;
  const bool global = std::fabs(ni * dlon) >= 359.0;
  const double period = 360.0 / std::fabs(dlon);
  if (fi < 0 || fi > ni - 1) {
    fi = std::fmod(fi, period);
    if (fi < 0) fi += period;
  }
  const double fj = (lat - lat1) / dlat;
  if (fj < 0 || fj > nj - 1) return qQNaN();
  if (!global && fi > ni - 1) return qQNaN();
  const int i0 = int(std::floor(fi)), j0 = int(std::floor(fj));
  const int i1 = global ? (i0 + 1) % ni : std::min(i0 + 1, ni - 1);
  const int j1 = std::min(j0 + 1, nj - 1);
  if (i0 >= ni) return qQNaN();
  const double ti = fi - i0, tj = fj - j0;
  const float v00 = values[qsizetype(j0) * ni + i0];
  const float v10 = values[qsizetype(j0) * ni + i1];
  const float v01 = values[qsizetype(j1) * ni + i0];
  const float v11 = values[qsizetype(j1) * ni + i1];
  if (std::isnan(v00) || std::isnan(v10) || std::isnan(v01) || std::isnan(v11))
    return qQNaN();
  return (v00 * (1 - ti) + v10 * ti) * (1 - tj) +
         (v01 * (1 - ti) + v11 * ti) * tj;
}

// --------------------------------------------------------------------------

bool GribFile::decodeGrib1(const uchar* m, qsizetype len, GribField& f,
                           QString* error) {
  auto fail = [&](const char* why) {
    if (error) *error = QString::fromLatin1(why);
    return false;
  };
  f.edition = 1;
  const uchar* pds = m + 8;
  const qsizetype pdsLen = u24(pds);
  if (8 + pdsLen > len) return fail("GRIB1: truncated PDS");
  const bool hasGds = pds[7] & 0x80, hasBms = pds[7] & 0x40;
  f.number = pds[8];
  f.levelType = pds[9];
  f.levelValue = u16(pds + 10);
  const int century = pds[24];
  const int yoc = pds[12];
  const int year = (century - 1) * 100 + yoc;
  f.refTime = QDateTime(QDate(year, pds[13], pds[14]), QTime(pds[15], pds[16]),
                        Qt::UTC);
  const int unit = pds[17], range = pds[20];
  qint64 p = pds[18];
  if (range == 10)
    p = u16(pds + 18);
  else if (range >= 2 && range <= 5)
    p = pds[19];
  f.validTime = addForecast(f.refTime, unit, p);
  const int decimal = s16(pds + 26);
  classify(f);

  const uchar* s = pds + pdsLen;
  if (!hasGds) return fail("GRIB1: predefined grids are not supported");
  const qsizetype gdsLen = u24(s);
  if (s[5] != 0) return fail("GRIB1: only lat/lon grids are supported");
  f.ni = int(u16(s + 6));
  f.nj = int(u16(s + 8));
  f.lat1 = s24(s + 10) / 1000.0;
  f.lon1 = s24(s + 13) / 1000.0;
  const double lat2 = s24(s + 17) / 1000.0;
  const double lon2 = s24(s + 20) / 1000.0;
  const int scan = s[27];
  double di = u16(s + 23) / 1000.0, dj = u16(s + 25) / 1000.0;
  if (s[16] & 0x80) {
    // Increments given; otherwise derive them from the corners.
  } else {
    di = f.ni > 1 ? std::fabs(lon2 - f.lon1) / (f.ni - 1) : 1;
    dj = f.nj > 1 ? std::fabs(lat2 - f.lat1) / (f.nj - 1) : 1;
  }
  f.dlon = (scan & 0x80) ? -di : di;
  f.dlat = (scan & 0x40) ? dj : -dj;
  s += gdsLen;

  const uchar* bitmap = nullptr;
  if (hasBms) {
    const qsizetype bmsLen = u24(s);
    if (u16(s + 4) != 0) return fail("GRIB1: predefined bitmaps unsupported");
    bitmap = s + 6;
    s += bmsLen;
  }
  const qsizetype bdsLen = u24(s);
  if (s[3] & 0xc0) return fail("GRIB1: only simple packing is supported");
  const int e = s16(s + 4);
  const double r = ibm32(s + 6);
  const int nbits = s[10];
  const qsizetype total = qsizetype(f.ni) * f.nj;
  qsizetype count = total;
  if (bitmap) {
    count = 0;
    for (qsizetype i = 0; i < total; ++i)
      count += (bitmap[i >> 3] >> (7 - (i & 7))) & 1;
  }
  QVector<float> packed(count);
  BitReader br(s + 11, bdsLen - 11);
  const double bs = std::pow(2.0, e), ds = std::pow(10.0, -decimal);
  for (qsizetype i = 0; i < count; ++i)
    packed[i] = float((r + (nbits ? br.get(nbits) : 0) * bs) * ds);
  applyBitmap(f, packed, bitmap);
  return true;
}

bool GribFile::decodeGrib2(const uchar* m, qsizetype len,
                           QVector<GribField>& out, QString* error) {
  auto fail = [&](const QString& why) {
    if (error) *error = why;
    return false;
  };
  const int discipline = m[6];
  qsizetype pos = 16;
  QDateTime ref;
  GribField grid;  // current grid definition
  GribField prod;  // current product definition
  // Data representation state
  int tmpl5 = -1;
  QByteArray rep;
  const uchar* bitmap = nullptr;
  const uchar* lastBitmap = nullptr;
  qsizetype npoints = 0;

  while (pos + 4 <= len) {
    if (std::memcmp(m + pos, "7777", 4) == 0) break;
    const qsizetype slen = u32(m + pos);
    const int num = m[pos + 4];
    const uchar* s = m + pos;
    if (slen < 5 || pos + slen > len) return fail("GRIB2: bad section");
    switch (num) {
      case 1:
        ref = QDateTime(QDate(int(u16(s + 12)), s[14], s[15]),
                        QTime(s[16], s[17], s[18]), Qt::UTC);
        break;
      case 3: {
        const int t = int(u16(s + 12));
        if (t != 0) return fail(QString("GRIB2: grid template 3.%1").arg(t));
        grid.ni = int(u32(s + 30));
        grid.nj = int(u32(s + 34));
        const quint32 basic = u32(s + 38), sub = u32(s + 42);
        const double unit = (basic == 0 || basic == 0xffffffff)
                                ? 1e-6
                                : double(basic) / double(sub);
        grid.lat1 = s32(s + 46) * unit;
        grid.lon1 = s32(s + 50) * unit;
        const double di = u32(s + 63) * unit, dj = u32(s + 67) * unit;
        const int scan = s[71];
        grid.dlon = (scan & 0x80) ? -di : di;
        grid.dlat = (scan & 0x40) ? dj : -dj;
        break;
      }
      case 4: {
        const int t = int(u16(s + 7));
        prod = GribField();
        prod.edition = 2;
        prod.discipline = discipline;
        prod.category = s[9];
        prod.number = s[10];
        prod.refTime = ref;
        if (t == 0 || t == 1 || t == 8 || t == 11) {
          prod.validTime = addForecast(ref, s[17], s32(s + 18));
          prod.levelType = s[22];
          const int scale = qint8(s[23]);
          prod.levelValue = s32(s + 24) * std::pow(10.0, -scale);
          if (t == 8 || t == 11) {
            // Statistically processed: valid at the end of the interval.
            const int off = t == 8 ? 34 : 37;
            if (off + 7 <= slen)
              prod.validTime =
                  QDateTime(QDate(int(u16(s + off)), s[off + 2], s[off + 3]),
                            QTime(s[off + 4], s[off + 5], s[off + 6]), Qt::UTC);
          }
        } else {
          prod.validTime = ref;
        }
        classify(prod);
        break;
      }
      case 5:
        npoints = qsizetype(u32(s + 5));
        tmpl5 = int(u16(s + 9));
        rep = QByteArray(reinterpret_cast<const char*>(s), int(slen));
        break;
      case 6:
        if (s[5] == 0) {
          bitmap = s + 6;
          lastBitmap = bitmap;
        } else if (s[5] == 254) {
          bitmap = lastBitmap;
        } else {
          bitmap = nullptr;
        }
        break;
      case 7: {
        GribField f = prod;
        f.ni = grid.ni;
        f.nj = grid.nj;
        f.lat1 = grid.lat1;
        f.lon1 = grid.lon1;
        f.dlat = grid.dlat;
        f.dlon = grid.dlon;
        const auto* r = reinterpret_cast<const uchar*>(rep.constData());
        if (rep.size() < 21) return fail("GRIB2: missing section 5");
        const double R = ieee32(r + 11);
        const int E = s16(r + 15), D = s16(r + 17);
        const int nbits = r[19];
        const double bs = std::pow(2.0, E), ds = std::pow(10.0, -D);
        const uchar* data = s + 5;
        const qsizetype dlen = slen - 5;
        QVector<float> packed(npoints);
        if (tmpl5 == 0) {
          BitReader br(data, dlen);
          for (qsizetype i = 0; i < npoints; ++i)
            packed[i] = float((R + (nbits ? br.get(nbits) : 0) * bs) * ds);
        } else if (tmpl5 == 2 || tmpl5 == 3) {
          if (rep.size() < 47) return fail("GRIB2: short template 5.2/5.3");
          const int missingMgmt = r[22];
          const quint32 ng = u32(r + 31);
          const int widthRef = r[35], widthBits = r[36];
          const quint32 lenRef = u32(r + 37);
          const int lenInc = r[41];
          const quint32 lastLen = u32(r + 42);
          const int lenBits = r[46];
          int order = 0, extraBytes = 0;
          if (tmpl5 == 3) {
            order = r[47];
            extraBytes = r[48];
          }
          BitReader br(data, dlen);
          qint64 ival1 = 0, ival2 = 0, minsd = 0;
          auto readSigned = [&](int bytes) {
            const quint32 v = br.get(bytes * 8);
            const quint32 signBit = 1u << (bytes * 8 - 1);
            return (v & signBit) ? -qint64(v & (signBit - 1)) : qint64(v);
          };
          if (order >= 1) ival1 = readSigned(extraBytes);
          if (order == 2) ival2 = readSigned(extraBytes);
          if (order >= 1) minsd = readSigned(extraBytes);
          QVector<qint64> gref(ng), gwidth(ng), glen(ng);
          for (quint32 g = 0; g < ng; ++g) gref[g] = br.get(nbits);
          br.align();
          for (quint32 g = 0; g < ng; ++g)
            gwidth[g] = br.get(widthBits) + widthRef;
          br.align();
          for (quint32 g = 0; g < ng; ++g)
            glen[g] = qint64(lenRef) + qint64(br.get(lenBits)) * lenInc;
          br.align();
          if (ng > 0) glen[ng - 1] = lastLen;
          QVector<qint64> ifld;
          QVector<bool> miss;
          ifld.reserve(npoints);
          miss.reserve(npoints);
          for (quint32 g = 0; g < ng; ++g) {
            const int w = int(gwidth[g]);
            const qint64 allOnesRef =
                (nbits > 0 && nbits < 63) ? (qint64(1) << nbits) - 1 : -1;
            for (qint64 k = 0; k < glen[g]; ++k) {
              qint64 v = w ? qint64(br.get(w)) : 0;
              bool isMissing = false;
              if (missingMgmt >= 1) {
                if (w) {
                  const qint64 ones = (qint64(1) << w) - 1;
                  isMissing = v == ones || (missingMgmt == 2 && v == ones - 1);
                } else {
                  isMissing = gref[g] == allOnesRef;
                }
              }
              ifld << (isMissing ? 0 : v + gref[g]);
              miss << isMissing;
            }
          }
          if (br.overrun()) return fail("GRIB2: truncated complex packing");
          if (ifld.size() < npoints) {
            ifld.resize(npoints);
            miss.resize(npoints);
          }
          // Undo spatial differencing over the non-missing values.
          if (order >= 1) {
            QVector<qint64> vals;
            QVector<qsizetype> idx;
            for (qsizetype i = 0; i < npoints; ++i)
              if (!miss[i]) {
                vals << ifld[i];
                idx << i;
              }
            if (!vals.isEmpty()) {
              vals[0] = ival1;
              if (order == 2 && vals.size() > 1) vals[1] = ival2;
              for (qsizetype i = order; i < vals.size(); ++i) {
                vals[i] += minsd;
                vals[i] +=
                    order == 1 ? vals[i - 1] : 2 * vals[i - 1] - vals[i - 2];
              }
            }
            for (qsizetype i = 0; i < idx.size(); ++i) ifld[idx[i]] = vals[i];
          }
          for (qsizetype i = 0; i < npoints; ++i)
            packed[i] = miss[i] ? kMissing : float((R + ifld[i] * bs) * ds);
        } else {
          return fail(
              QString("GRIB2: data template 5.%1 not supported").arg(tmpl5));
        }
        applyBitmap(f, packed, bitmap);
        out << f;
        break;
      }
      default:
        break;
    }
    pos += slen;
  }
  return true;
}

bool GribFile::parse(const QByteArray& data, QString* error) {
  const auto* d = reinterpret_cast<const uchar*>(data.constData());
  const qsizetype n = data.size();
  qsizetype pos = 0;
  QString lastError;
  while (pos + 16 <= n) {
    const qsizetype start = data.indexOf("GRIB", pos);
    if (start < 0 || start + 16 > n) break;
    const uchar* m = d + start;
    const int edition = m[7];
    qsizetype len = 0;
    if (edition == 1)
      len = qsizetype(u24(m + 4));
    else if (edition == 2)
      len = qsizetype(u64(m + 8));
    if (len < 16 || start + len > n) {
      pos = start + 4;
      continue;
    }
    QString err;
    if (edition == 1) {
      GribField f;
      if (decodeGrib1(m, len, f, &err))
        m_fields << f;
      else
        lastError = err;
    } else {
      if (!decodeGrib2(m, len, m_fields, &err)) lastError = err;
    }
    pos = start + len;
  }
  buildSteps();
  if (m_fields.isEmpty()) {
    if (error)
      *error = lastError.isEmpty() ? QStringLiteral("No GRIB records found")
                                   : lastError;
    return false;
  }
  return true;
}

bool GribFile::load(const QString& path, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = f.errorString();
    return false;
  }
  m_path = path;
  return parse(f.readAll(), error);
}

void GribFile::buildSteps() {
  QMap<QDateTime, GribTimeStep> byTime;
  using P = GribField::Param;
  for (const GribField& f : m_fields) {
    GribTimeStep& s = byTime[f.validTime];
    s.time = f.validTime;
    // Prefer 10 m wind (level type 103 in GRIB2, 105 in GRIB1).
    const bool tenMeter =
        (f.levelType == 103 || f.levelType == 105) && f.levelValue == 10;
    switch (f.param) {
      case P::WindU:
        if (!s.windU || tenMeter) s.windU = &f;
        break;
      case P::WindV:
        if (!s.windV || tenMeter) s.windV = &f;
        break;
      case P::Pressure:
        s.pressure = &f;
        break;
      case P::Gust:
        s.gust = &f;
        break;
      case P::WaveHeight:
        s.waves = &f;
        break;
      default:
        break;
    }
  }
  m_steps = byTime.values().toVector();
}

bool GribFile::wind(int step, double lat, double lon, double& speedKn,
                    double& dirFrom) const {
  if (step < 0 || step >= m_steps.size()) return false;
  const GribTimeStep& s = m_steps[step];
  if (!s.windU || !s.windV) return false;
  const double u = s.windU->sample(lat, lon), v = s.windV->sample(lat, lon);
  if (std::isnan(u) || std::isnan(v)) return false;
  speedKn = std::hypot(u, v) * 1.943844;
  dirFrom = std::fmod(std::atan2(-u, -v) * 180 / M_PI + 360, 360);
  return true;
}

// --------------------------------------------------------------------------

GribManager& GribManager::instance() {
  static GribManager m;
  return m;
}

bool GribManager::open(const QString& path, QString* error) {
  auto f = std::make_shared<GribFile>();
  if (!f->load(path, error)) return false;
  m_file = f;
  emit fileChanged();
  selectNow();
  return true;
}

void GribManager::close() {
  m_file.reset();
  m_step = 0;
  emit fileChanged();
  emit stepChanged();
}

void GribManager::setStep(int step) {
  if (!m_file || m_file->steps().isEmpty()) return;
  step = qBound(0, step, int(m_file->steps().size()) - 1);
  if (step == m_step) return;
  m_step = step;
  emit stepChanged();
}

void GribManager::selectNow() {
  if (!m_file) return;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  int best = 0;
  qint64 bestDiff = std::numeric_limits<qint64>::max();
  for (int i = 0; i < m_file->steps().size(); ++i) {
    const qint64 d = std::abs(m_file->steps()[i].time.secsTo(now));
    if (d < bestDiff) {
      bestDiff = d;
      best = i;
    }
  }
  m_step = best;
  emit stepChanged();
}

const GribTimeStep* GribManager::currentStep() const {
  if (!m_file || m_step >= m_file->steps().size()) return nullptr;
  return &m_file->steps()[m_step];
}

void GribManager::setOptions(bool wind, bool pressure, bool heatmap) {
  showWind = wind;
  showPressure = pressure;
  showHeatmap = heatmap;
  emit optionsChanged();
}
