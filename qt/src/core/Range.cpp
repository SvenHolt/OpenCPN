#include "core/Range.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

#include "core/BoatConfig.h"
#include "core/GeoMath.h"

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Range", s); }
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kBaseC = 18;
// Solar: kWh/m² of radiation times peak power times performance ratio.
constexpr double kPerformance = 0.75;
// The heater cannot burn more than at full power all day (Air Top 2000:
// 0.28 l/h, 34 W).
constexpr double kHeaterMaxLpd = 0.28 * 24;
constexpr double kHeaterMaxWhPerDay = 34 * 24;
}  // namespace

double DayForecast::degreeDays() const {
  // The day spends about half its time near the minimum and half near the
  // maximum; this catches cold nights of mild days.
  return (std::max(0.0, kBaseC - minC) + std::max(0.0, kBaseC - maxC)) / 2;
}

UsageRates UsageRates::defaults(BoatMode mode) {
  UsageRates r;
  r.mode = mode;
  r.freshLpd = mode == BoatMode::Underway ? 18 : 22;
  r.wasteLpd = r.freshLpd / 3;
  r.heaterFuelPerDd = 0.12;
  r.heaterWhPerDd = 12;
  r.baseWhPerDay = mode == BoatMode::Underway ? 1600 : 1100;
  r.solarWhPerDay = 600;
  if (mode == BoatMode::Underway) {
    r.engineHpd = 2.5;
    r.engineFuelLpd = 2.5 * BoatConfig::instance().consumptionLph;
    r.alternatorWhPerDay = 2.5 * 600;
  }
  return r;
}

RangeState RangeState::fromBoat() {
  const auto& sys = BoatSystems::instance();
  RangeState s;
  if (const BoatTank* t = sys.tank(BoatTank::Kind::FreshWater)) {
    s.freshL = t->level * t->capacityL;
    s.freshCapacityL = t->capacityL;
  }
  if (const BoatTank* t = sys.tank(BoatTank::Kind::WasteWater)) {
    s.wasteL = t->level * t->capacityL;
    s.wasteCapacityL = t->capacityL;
  }
  if (const BoatTank* t = sys.tank(BoatTank::Kind::Fuel)) {
    s.fuelL = t->level * t->capacityL;
    s.fuelCapacityL = t->capacityL;
    s.fuelReserveL = 0.1 * t->capacityL;
  }
  const EnergySystem& e = sys.energy();
  s.soc = e.soc;
  s.capacityAh = e.capacityAh;
  s.solarPeakW = e.solarPeakW;
  return s;
}

namespace range {

QString resourceName(RangeResource r) {
  switch (r) {
    case RangeResource::FreshWater:
      return tr("Fresh water");
    case RangeResource::WasteWater:
      return tr("Holding tank");
    case RangeResource::Energy:
      return tr("Battery");
    case RangeResource::Diesel:
      return tr("Diesel");
  }
  return {};
}

QPair<QDateTime, QDateTime> lastTrip(const QVector<ConsumptionSample>& log) {
  // Walk back from the end: skip shore power at the end, then collect the
  // samples away from it.
  int i = int(log.size()) - 1;
  while (i >= 0 && log[i].mode == BoatMode::Marina) --i;
  if (i < 0) return {};
  const QDateTime to = log[i].end;
  QDateTime from = log[i].start();
  for (--i; i >= 0; --i) {
    if (log[i].mode == BoatMode::Marina) break;
    if (log[i].end.secsTo(from) > 3 * 3600) break;
    from = log[i].start();
  }
  return {from, to};
}

QPair<QDateTime, QDateTime> window(const QVector<ConsumptionSample>& log,
                                   RangeWindow w, const QDateTime& now) {
  if (log.isEmpty()) return {};
  if (w == RangeWindow::LastTrip) return lastTrip(log);
  const int days = w == RangeWindow::Days3   ? 3
                   : w == RangeWindow::Days7 ? 7
                                             : 14;
  return {now.addDays(-days), now};
}

UsageRates learn(const QVector<ConsumptionSample>& samples, BoatMode mode) {
  // Calendar days (local time) are classified: underway with two or more
  // hours underway, marina with six or more hours on shore power, else at
  // anchor. The rates are totals per day of that kind; partial days count
  // with their logged share.
  struct Day {
    double hours = 0, underwayH = 0, marinaH = 0, tempH = 0;
    double fresh = 0, waste = 0, heaterFuel = 0, engineFuel = 0, engineH = 0;
    double loads = 0, heaterWh = 0, solar = 0, alternator = 0, dd = 0;
  };
  QMap<QDate, Day> days;
  for (const auto& s : samples) {
    Day& d = days[s.end.toLocalTime().date()];
    d.hours += s.hours;
    if (s.mode == BoatMode::Underway) d.underwayH += s.hours;
    if (s.mode == BoatMode::Marina) d.marinaH += s.hours;
    d.tempH += s.outsideC * s.hours;
    d.fresh += s.freshL;
    d.waste += s.wasteL;
    d.heaterFuel += s.heaterFuelL;
    d.engineFuel += s.engineFuelL;
    d.engineH += s.engineH;
    d.loads += s.loadsWh;
    d.heaterWh += s.heaterWh;
    d.solar += s.solarWh;
    d.alternator += s.alternatorWh;
    d.dd += s.degreeDays();
  }
  Day sum;
  for (const Day& d : days) {
    const BoatMode kind = d.underwayH >= 2 ? BoatMode::Underway
                          : d.marinaH >= 6 ? BoatMode::Marina
                                           : BoatMode::Anchor;
    if (kind != mode) continue;
    sum.hours += d.hours;
    sum.tempH += d.tempH;
    sum.fresh += d.fresh;
    sum.waste += d.waste;
    sum.heaterFuel += d.heaterFuel;
    sum.engineFuel += d.engineFuel;
    sum.engineH += d.engineH;
    sum.loads += d.loads;
    sum.heaterWh += d.heaterWh;
    sum.solar += d.solar;
    sum.alternator += d.alternator;
    sum.dd += d.dd;
  }
  UsageRates r = UsageRates::defaults(mode);
  r.days = sum.hours / 24;
  if (r.days < 0.5) return r;  // too little: defaults
  r.learned = true;
  const double n = r.days;
  r.meanTempC = sum.tempH / sum.hours;
  r.degreeDaysPerDay = sum.dd / n;
  r.freshLpd = sum.fresh / n;
  r.wasteLpd = sum.waste / n;
  r.engineFuelLpd = sum.engineFuel / n;
  r.engineHpd = sum.engineH / n;
  r.baseWhPerDay = (sum.loads - sum.heaterWh) / n;
  r.solarWhPerDay = sum.solar / n;
  r.alternatorWhPerDay = sum.alternator / n;
  // Heating per degree day; with hardly any cold the defaults stay.
  if (sum.dd >= 0.5) {
    r.heaterFuelPerDd = sum.heaterFuel / sum.dd;
    r.heaterWhPerDd = sum.heaterWh / sum.dd;
  }
  return r;
}

RangeResult project(const UsageRates& rates, const RangeState& state,
                    const QVector<DayForecast>& forecast, const QDate& today,
                    int horizon) {
  RangeResult res;
  res.mode = rates.mode;
  res.rates = rates;
  QHash<QDate, DayForecast> byDate;
  double meanC = 0;
  for (const auto& d : forecast) {
    byDate.insert(d.date, d);
    meanC += d.meanC();
  }
  const bool haveForecast = !forecast.isEmpty();
  if (haveForecast) meanC /= forecast.size();

  const double whPerAh = state.voltage;
  const double maxWh = (1 - state.minSoc) * state.capacityAh * whPerAh;
  double fresh = state.freshL;
  double wasteFree = std::max(0.0, state.wasteCapacityL - state.wasteL);
  double fuel = std::max(0.0, state.fuelL - state.fuelReserveL);
  double battery = std::clamp(state.soc - state.minSoc, 0.0, 1.0) *
                   state.capacityAh * whPerAh;
  res.resources[0] = {RangeResource::FreshWater, fresh, 0, kInf};
  res.resources[1] = {RangeResource::WasteWater, wasteFree, 0, kInf};
  res.resources[2] = {RangeResource::Energy, battery, 0, kInf};
  res.resources[3] = {RangeResource::Diesel, fuel, 0, kInf};
  double sumUse[4] = {0, 0, 0, 0};

  const auto consume = [&](int k, double& left, double use, int day) {
    if (use > 0 && std::isinf(res.resources[k].days) && left - use <= 0)
      res.resources[k].days = day + std::max(0.0, left) / use;
    left -= use;
  };
  for (int day = 0; day < horizon; ++day) {
    ProjectionDay p;
    p.date = today.addDays(day);
    double dd;
    if (byDate.contains(p.date)) {
      const DayForecast& f = byDate[p.date];
      p.tempC = f.meanC();
      dd = f.degreeDays();
      p.forecast = true;
      p.solarWh = std::isnan(f.radiationMJ)
                      ? rates.solarWhPerDay
                      : state.solarPeakW * f.radiationMJ / 3.6 * kPerformance;
    } else {
      // Beyond the forecast: its mean, or what the log saw.
      p.tempC = haveForecast ? meanC : rates.meanTempC;
      dd =
          haveForecast ? std::max(0.0, kBaseC - meanC) : rates.degreeDaysPerDay;
      p.solarWh = rates.solarWhPerDay;
    }
    p.degreeDays = dd;
    p.heaterFuelL = std::min(kHeaterMaxLpd, rates.heaterFuelPerDd * dd);
    const double fuelUse = p.heaterFuelL + rates.engineFuelLpd;
    p.netWh = rates.baseWhPerDay +
              std::min(kHeaterMaxWhPerDay, rates.heaterWhPerDd * dd) -
              p.solarWh - rates.alternatorWhPerDay;
    consume(0, fresh, rates.freshLpd, day);
    consume(1, wasteFree, rates.wasteLpd, day);
    consume(3, fuel, fuelUse, day);
    // The battery recharges up to full on sunny days.
    if (p.netWh > 0)
      consume(2, battery, p.netWh, day);
    else
      battery = std::min(maxWh, battery - p.netWh);
    sumUse[0] += rates.freshLpd;
    sumUse[1] += rates.wasteLpd;
    sumUse[2] += p.netWh;
    sumUse[3] += fuelUse;
    p.freshL = std::max(0.0, fresh);
    p.wasteL = state.wasteCapacityL - std::max(0.0, wasteFree);
    p.fuelL = std::max(0.0, fuel) + state.fuelReserveL;
    p.batteryPct = 100 * (state.minSoc + std::max(0.0, battery) /
                                             (state.capacityAh * whPerAh));
    res.projection << p;
  }
  // Mean daily use over the first week (or until the forecast ends).
  const int n = std::max(1, int(res.projection.size()));
  for (int k = 0; k < 4; ++k) res.resources[k].perDay = sumUse[k] / n;
  for (const auto& r : res.resources)
    if (r.days < res.days) {
      res.days = r.days;
      res.limit = r.kind;
    }
  return res;
}

}  // namespace range

// --------------------------------------------------------------------------

TemperatureForecast& TemperatureForecast::instance() {
  static TemperatureForecast s;
  return s;
}

QString TemperatureForecast::file() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/temperature-forecast.json";
}

TemperatureForecast::TemperatureForecast() {
  QFile f(file());
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  m_fetched = QDateTime::fromString(o["fetched"].toString(), Qt::ISODate);
  m_pos = QPointF(o["lon"].toDouble(), o["lat"].toDouble());
  m_days = parse(QJsonDocument(o["data"].toObject()).toJson(), nullptr);
}

void TemperatureForecast::store() const {
  QJsonArray time, tmin, tmax, rad;
  for (const auto& d : m_days) {
    time << d.date.toString(Qt::ISODate);
    tmin << d.minC;
    tmax << d.maxC;
    rad << (std::isnan(d.radiationMJ) ? QJsonValue() : d.radiationMJ);
  }
  const QJsonObject data{
      {"daily", QJsonObject{{"time", time},
                            {"temperature_2m_min", tmin},
                            {"temperature_2m_max", tmax},
                            {"shortwave_radiation_sum", rad}}}};
  const QString path = file();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(
      QJsonDocument(QJsonObject{{"fetched", m_fetched.toString(Qt::ISODate)},
                                {"lat", m_pos.y()},
                                {"lon", m_pos.x()},
                                {"data", data}})
          .toJson(QJsonDocument::Compact));
  f.commit();
}

QVector<DayForecast> TemperatureForecast::upcoming(const QDate& today) const {
  QVector<DayForecast> out;
  for (const auto& d : m_days)
    if (d.date >= today) out << d;
  return out;
}

void TemperatureForecast::setDays(const QVector<DayForecast>& days,
                                  const QPointF& pos,
                                  const QDateTime& fetched) {
  m_days = days;
  m_pos = pos;
  m_fetched = fetched;
  store();
  emit updated();
}

QVector<DayForecast> TemperatureForecast::parse(const QByteArray& json,
                                                QString* error) {
  QVector<DayForecast> out;
  const QJsonObject daily =
      QJsonDocument::fromJson(json).object().value("daily").toObject();
  const QJsonArray time = daily["time"].toArray();
  const QJsonArray tmin = daily["temperature_2m_min"].toArray();
  const QJsonArray tmax = daily["temperature_2m_max"].toArray();
  const QJsonArray rad = daily["shortwave_radiation_sum"].toArray();
  for (int i = 0; i < time.size(); ++i) {
    if (tmin.at(i).isNull() || tmax.at(i).isNull() || !tmin.at(i).isDouble() ||
        !tmax.at(i).isDouble())
      continue;
    DayForecast d;
    d.date = QDate::fromString(time.at(i).toString(), Qt::ISODate);
    d.minC = tmin.at(i).toDouble();
    d.maxC = tmax.at(i).toDouble();
    if (rad.at(i).isDouble()) d.radiationMJ = rad.at(i).toDouble();
    if (d.date.isValid()) out << d;
  }
  if (out.isEmpty() && error) *error = tr("No temperature forecast.");
  return out;
}

void TemperatureForecast::update(double lat, double lon, int maxAgeH) {
  const bool fresh =
      m_fetched.isValid() &&
      m_fetched.secsTo(QDateTime::currentDateTimeUtc()) < maxAgeH * 3600 &&
      geo::distanceNm(lat, lon, m_pos.y(), m_pos.x()) < 30;
  if (!fresh && !m_busy) fetch(lat, lon);
}

void TemperatureForecast::fetch(double lat, double lon) {
  QUrl url("https://api.open-meteo.com/v1/forecast");
  QUrlQuery q;
  q.addQueryItem("latitude", QString::number(lat, 'f', 2));
  q.addQueryItem("longitude", QString::number(lon, 'f', 2));
  q.addQueryItem(
      "daily", "temperature_2m_min,temperature_2m_max,shortwave_radiation_sum");
  q.addQueryItem("forecast_days", "16");
  q.addQueryItem("timezone", "auto");
  url.setQuery(q);
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader,
                "OpenCPN-Qt/0.1 (range analysis)");
  req.setTransferTimeout(30000);
  m_busy = true;
  QNetworkReply* r = m_net.get(req);
  connect(r, &QNetworkReply::finished, this, [this, r, lat, lon] {
    r->deleteLater();
    m_busy = false;
    const QByteArray body = r->readAll();
    if (r->error() != QNetworkReply::NoError) {
      const QString reason =
          QJsonDocument::fromJson(body).object().value("reason").toString();
      emit failed(reason.isEmpty() ? r->errorString() : reason);
      return;
    }
    QString err;
    const auto days = parse(body, &err);
    if (days.isEmpty()) {
      emit failed(err);
      return;
    }
    setDays(days, QPointF(lon, lat), QDateTime::currentDateTimeUtc());
  });
}
