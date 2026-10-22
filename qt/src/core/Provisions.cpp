#include "core/Provisions.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace {
QString tr(const char* s) {
  return QCoreApplication::translate("Provisions", s);
}
const char* kCategories[] = {"fruit", "vegetables", "preserves",
                             "cans",  "fridge",     "freezer"};

QString amount(double q, const QString& unit) {
  return QStringLiteral("%1 %2").arg(
      QString::number(q, 'f', q == std::round(q) ? 0 : 1), unit);
}
}  // namespace

QJsonObject ProvisionItem::toJson() const {
  QJsonArray use;
  for (const auto& u : usage)
    use.append(
        QJsonObject{{"date", u.date.toString(Qt::ISODate)}, {"q", u.quantity}});
  return {{"id", id},
          {"name", name},
          {"category", kCategories[int(category)]},
          {"quantity", quantity},
          {"unit", unit},
          {"added", added.toString(Qt::ISODate)},
          {"bestBefore", bestBefore.toString(Qt::ISODate)},
          {"planned", plannedPerPersonDay},
          {"min", minQuantity},
          {"usage", use}};
}

ProvisionItem ProvisionItem::fromJson(const QJsonObject& o) {
  ProvisionItem i;
  i.id = o["id"].toString();
  i.name = o["name"].toString();
  const QString c = o["category"].toString();
  for (int k = 0; k < int(std::size(kCategories)); ++k)
    if (c == kCategories[k]) i.category = ProvisionCategory(k);
  i.quantity = std::max(0.0, o["quantity"].toDouble());
  i.unit = o["unit"].toString("pcs");
  i.added = QDate::fromString(o["added"].toString(), Qt::ISODate);
  i.bestBefore = QDate::fromString(o["bestBefore"].toString(), Qt::ISODate);
  i.plannedPerPersonDay = o["planned"].toDouble();
  i.minQuantity = o["min"].toDouble();
  for (const auto& v : o["usage"].toArray()) {
    const QJsonObject u = v.toObject();
    ProvisionUse use{QDate::fromString(u["date"].toString(), Qt::ISODate),
                     u["q"].toDouble()};
    if (use.date.isValid() && use.quantity > 0) i.usage << use;
  }
  return i;
}

// --------------------------------------------------------------------------

ProvisionStore& ProvisionStore::instance() {
  static ProvisionStore s;
  return s;
}

ProvisionStore::ProvisionStore() { load(); }

QString ProvisionStore::file() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/provisions.json";
}

void ProvisionStore::load() {
  m_items.clear();
  QFile f(file());
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
  m_crew = std::clamp(root["crew"].toInt(2), 1, 20);
  m_tripDays = std::clamp(root["tripDays"].toInt(7), 1, 365);
  for (const auto& v : root["items"].toArray()) {
    ProvisionItem i = ProvisionItem::fromJson(v.toObject());
    if (!i.id.isEmpty()) m_items << i;
  }
}

void ProvisionStore::reload() {
  load();
  emit changed();
}

void ProvisionStore::store() const {
  QJsonArray arr;
  for (const auto& i : m_items) arr.append(i.toJson());
  const QString path = file();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(QJsonDocument(QJsonObject{{"crew", m_crew},
                                    {"tripDays", m_tripDays},
                                    {"items", arr}})
              .toJson());
  f.commit();
}

const ProvisionItem* ProvisionStore::find(const QString& id) const {
  for (const auto& i : m_items)
    if (i.id == id) return &i;
  return nullptr;
}

QVector<ProvisionItem> ProvisionStore::inCategory(ProvisionCategory c) const {
  QVector<ProvisionItem> out;
  for (const auto& i : m_items)
    if (i.category == c) out << i;
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    return a.name.localeAwareCompare(b.name) < 0;
  });
  return out;
}

QString ProvisionStore::save(const ProvisionItem& item) {
  ProvisionItem i = item;
  if (i.id.isEmpty())
    i.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  if (!i.added.isValid()) i.added = QDate::currentDate();
  auto it = std::find_if(m_items.begin(), m_items.end(),
                         [&](const auto& x) { return x.id == i.id; });
  if (it != m_items.end())
    *it = i;
  else
    m_items << i;
  store();
  emit changed();
  return i.id;
}

void ProvisionStore::remove(const QString& id) {
  if (m_items.removeIf([&](const auto& i) { return i.id == id; }) == 0) return;
  store();
  emit changed();
}

void ProvisionStore::take(const QString& id, double quantity,
                          const QDate& date) {
  for (auto& i : m_items) {
    if (i.id != id) continue;
    const double q = std::min(quantity, i.quantity);
    if (q <= 0) return;
    i.quantity -= q;
    i.usage << ProvisionUse{date, q};
    store();
    emit changed();
    return;
  }
}

void ProvisionStore::count(const QString& id, double counted,
                           const QDate& date) {
  for (auto& i : m_items) {
    if (i.id != id) continue;
    counted = std::max(0.0, counted);
    if (counted < i.quantity)
      i.usage << ProvisionUse{date, i.quantity - counted};
    i.quantity = counted;
    store();
    emit changed();
    return;
  }
}

void ProvisionStore::clear() {
  m_items.clear();
  store();
  emit changed();
}

void ProvisionStore::setTrip(int crew, int days) {
  m_crew = std::clamp(crew, 1, 20);
  m_tripDays = std::clamp(days, 1, 365);
  store();
  emit changed();
}

QString ProvisionStore::categoryName(ProvisionCategory c) {
  switch (c) {
    case ProvisionCategory::Fruit:
      return tr("Fruit");
    case ProvisionCategory::Vegetables:
      return tr("Vegetables");
    case ProvisionCategory::Preserves:
      return tr("Preserves");
    case ProvisionCategory::Cans:
      return tr("Cans");
    case ProvisionCategory::Fridge:
      return tr("Fridge");
    case ProvisionCategory::Freezer:
      return tr("Freezer");
  }
  return {};
}

QString ProvisionStore::categoryIcon(ProvisionCategory c) {
  switch (c) {
    case ProvisionCategory::Fruit:
      return "apple";
    case ProvisionCategory::Vegetables:
      return "carrot";
    case ProvisionCategory::Preserves:
      return "jar";
    case ProvisionCategory::Cans:
      return "can";
    case ProvisionCategory::Fridge:
      return "fridge";
    case ProvisionCategory::Freezer:
      return "snow";
  }
  return {};
}

QVector<ProvisionCategory> ProvisionStore::categories() {
  return {ProvisionCategory::Fruit,     ProvisionCategory::Vegetables,
          ProvisionCategory::Preserves, ProvisionCategory::Cans,
          ProvisionCategory::Fridge,    ProvisionCategory::Freezer};
}

int ProvisionStore::shelfLifeDays(ProvisionCategory c) {
  switch (c) {
    case ProvisionCategory::Fruit:
      return 7;
    case ProvisionCategory::Vegetables:
      return 10;
    case ProvisionCategory::Preserves:
      return 365;
    case ProvisionCategory::Cans:
      return 730;
    case ProvisionCategory::Fridge:
      return 7;
    case ProvisionCategory::Freezer:
      return 90;
  }
  return 30;
}

double ProvisionStore::measuredPerDay(const ProvisionItem& item,
                                      const QDate& today, int days) {
  // From the first use within the window (an item bought three days ago
  // is not diluted over two weeks).
  const QDate from = today.addDays(-days + 1);
  double sum = 0;
  QDate first;
  for (const auto& u : item.usage) {
    if (u.date < from || u.date > today) continue;
    sum += u.quantity;
    if (!first.isValid() || u.date < first) first = u.date;
  }
  if (sum <= 0) return 0;
  QDate start = first;
  if (item.added.isValid() && item.added < start) start = item.added;
  start = std::max(start, from);
  return sum / std::max<qint64>(1, start.daysTo(today) + 1);
}

double ProvisionStore::usePerDay(const ProvisionItem& item,
                                 const QDate& today) const {
  const double measured = measuredPerDay(item, today);
  if (measured > 0) return measured;
  return item.plannedPerPersonDay * m_crew;
}

double ProvisionStore::daysLeft(const ProvisionItem& item,
                                const QDate& today) const {
  const double use = usePerDay(item, today);
  if (use <= 0) return -1;
  return item.quantity / use;
}

QVector<ProvisionIssue> ProvisionStore::check(const QDate& today) const {
  using K = ProvisionIssue::Kind;
  QVector<ProvisionIssue> out;
  for (const auto& i : m_items) {
    if (i.quantity <= 0 && i.minQuantity <= 0) continue;
    const QString what =
        QStringLiteral("%1 (%2)").arg(i.name, amount(i.quantity, i.unit));
    if (i.bestBefore.isValid() && i.quantity > 0) {
      const qint64 toExpiry = today.daysTo(i.bestBefore);
      if (toExpiry < 0) {
        out << ProvisionIssue{K::Expired, i.id,
                              tr("%1 is past its best-before date (%2).")
                                  .arg(what, i.bestBefore.toString("dd.MM."))};
        continue;
      }
      const double left = daysLeft(i, today);
      if (left > toExpiry + 0.5) {
        const double spoiled = i.quantity - usePerDay(i, today) * toExpiry;
        out << ProvisionIssue{
            K::WillSpoil, i.id,
            tr("%1: about %2 will not be used before %3 – use more or "
               "share.")
                .arg(i.name, amount(std::max(0.0, spoiled), i.unit),
                     i.bestBefore.toString("dd.MM."))};
      } else if (toExpiry <= 3) {
        out << ProvisionIssue{
            K::ExpiresSoon, i.id,
            toExpiry == 0 ? tr("%1 expires today.").arg(what)
            : toExpiry == 1
                ? tr("%1 expires tomorrow.").arg(what)
                : tr("%1 expires in %2 days.").arg(what).arg(toExpiry)};
      }
    }
    const double left = daysLeft(i, today);
    if (left >= 0 && left < m_tripDays)
      out << ProvisionIssue{K::RunsOut, i.id,
                            tr("%1 lasts %2 days, the trip %3 days.")
                                .arg(what)
                                .arg(left, 0, 'f', 1)
                                .arg(m_tripDays)};
    if (i.minQuantity > 0 && i.quantity < i.minQuantity)
      out << ProvisionIssue{K::BelowMin, i.id,
                            tr("%1 is below the minimum of %2.")
                                .arg(what, amount(i.minQuantity, i.unit))};
  }
  std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    return int(a.kind) < int(b.kind);
  });
  return out;
}

QVector<ShoppingEntry> ProvisionStore::shoppingList(const QDate& today) const {
  QVector<ShoppingEntry> out;
  for (const auto& i : m_items) {
    const double use = usePerDay(i, today);
    // Usable stock: what is not past its best-before date during the trip.
    double usable = i.quantity;
    if (i.bestBefore.isValid()) {
      const qint64 toExpiry = today.daysTo(i.bestBefore);
      if (toExpiry < 0)
        usable = 0;
      else if (use > 0)
        usable = std::min(usable, use * toExpiry);
    }
    double need = std::max(use * m_tripDays, i.minQuantity) - usable;
    if (need <= 0.01) continue;
    // Whole pieces, cans and jars.
    if (i.unit == "pcs" || i.unit == "can" || i.unit == "jar" ||
        i.unit == "pack")
      need = std::ceil(need);
    out << ShoppingEntry{i.id, i.name, i.unit, need};
  }
  return out;
}

CategorySummary ProvisionStore::summary(ProvisionCategory c,
                                        const QDate& today) const {
  CategorySummary s{c};
  for (const auto& i : m_items) {
    if (i.category != c || i.quantity <= 0) continue;
    ++s.items;
    if (i.bestBefore.isValid() && (!s.earliestBestBefore.isValid() ||
                                   i.bestBefore < s.earliestBestBefore))
      s.earliestBestBefore = i.bestBefore;
    const double left = daysLeft(i, today);
    if (left >= 0 && (s.daysCovered < 0 || left < s.daysCovered))
      s.daysCovered = left;
  }
  return s;
}

void ProvisionStore::loadExamples(const QDate& today) {
  const auto add = [&](const QString& name, ProvisionCategory c, double q,
                       const QString& unit, int bestBeforeDays, double perDay,
                       double minQ = 0) {
    ProvisionItem i;
    i.name = name;
    i.category = c;
    i.quantity = q;
    i.unit = unit;
    i.added = today.addDays(-3);
    i.bestBefore = today.addDays(bestBeforeDays);
    i.plannedPerPersonDay = perDay;
    i.minQuantity = minQ;
    i.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    m_items << i;
    return &m_items.last();
  };
  using C = ProvisionCategory;
  m_items.clear();
  auto* apples = add(tr("Apples"), C::Fruit, 8, "pcs", 12, 0.5);
  apples->usage = {{today.addDays(-2), 2}, {today.addDays(-1), 1}, {today, 1}};
  add(tr("Bananas"), C::Fruit, 6, "pcs", 3, 0.5);
  add(tr("Lemons"), C::Fruit, 4, "pcs", 20, 0.1);
  add(tr("Potatoes"), C::Vegetables, 2.5, "kg", 25, 0.15);
  auto* tomatoes = add(tr("Tomatoes"), C::Vegetables, 6, "pcs", 4, 0.5);
  tomatoes->usage = {{today.addDays(-2), 1}};
  add(tr("Onions"), C::Vegetables, 1, "kg", 30, 0.05);
  add(tr("Carrots"), C::Vegetables, 1, "kg", 12, 0.1);
  add(tr("Jam"), C::Preserves, 2, "jar", 200, 0.05);
  add(tr("Pickles"), C::Preserves, 1, "jar", 300, 0);
  add(tr("Tomato sauce"), C::Preserves, 3, "jar", 250, 0.1, 1);
  add(tr("Tuna"), C::Cans, 4, "can", 700, 0.1, 2);
  add(tr("Chickpeas"), C::Cans, 3, "can", 600, 0.1);
  add(tr("Ravioli"), C::Cans, 2, "can", 500, 0.05, 2);
  auto* milk = add(tr("Milk"), C::Fridge, 3, "l", 6, 0.25, 1);
  milk->usage = {{today.addDays(-3), 0.5},
                 {today.addDays(-2), 0.5},
                 {today.addDays(-1), 0.5},
                 {today, 0.5}};
  add(tr("Butter"), C::Fridge, 1, "pack", 20, 0.05);
  add(tr("Cheese"), C::Fridge, 0.4, "kg", 14, 0.03);
  add(tr("Yoghurt"), C::Fridge, 4, "pcs", 2, 0.5);
  add(tr("Fish fillet"), C::Freezer, 0.8, "kg", 80, 0);
  add(tr("Bread rolls"), C::Freezer, 12, "pcs", 60, 1);
  store();
  emit changed();
}
