// Provisions on board: fruit, vegetables, preserves (jars), cans, fridge
// and freezer. Every item has a quantity, a best-before date and a log of
// what was taken; the check compares stock, use and shelf life with the
// planned trip (crew and days). Stored in AppData/provisions.json.
#pragma once

#include <QDate>
#include <QJsonObject>
#include <QObject>
#include <QVector>

enum class ProvisionCategory {
  Fruit,
  Vegetables,
  Preserves,
  Cans,
  Fridge,
  Freezer
};

struct ProvisionUse {
  QDate date;
  double quantity = 0;  // taken (positive)
};

struct ProvisionItem {
  QString id, name;
  ProvisionCategory category = ProvisionCategory::Fruit;
  double quantity = 0;
  QString unit = "pcs";  // pcs, kg, g, l, pack, can, jar
  QDate added, bestBefore;
  /** Planned use per person and day (0: unknown, the log decides). */
  double plannedPerPersonDay = 0;
  double minQuantity = 0;  // reorder below
  QVector<ProvisionUse> usage;

  QJsonObject toJson() const;
  static ProvisionItem fromJson(const QJsonObject& o);
};

/** A finding of the consumption check. */
struct ProvisionIssue {
  enum class Kind {
    Expired,      // past best-before
    ExpiresSoon,  // within 3 days
    WillSpoil,    // expires before it is used up at the current use
    RunsOut,      // used up before the end of the trip
    BelowMin,     // below the minimum stock
  };
  Kind kind;
  QString itemId;
  QString text;
};

struct ShoppingEntry {
  QString itemId, name, unit;
  double quantity = 0;
};

struct CategorySummary {
  ProvisionCategory category;
  int items = 0;
  QDate earliestBestBefore;
  double daysCovered = -1;  // shortest days left with known use, -1: unknown
};

class ProvisionStore : public QObject {
  Q_OBJECT
public:
  static ProvisionStore& instance();

  const QVector<ProvisionItem>& items() const { return m_items; }
  const ProvisionItem* find(const QString& id) const;
  QVector<ProvisionItem> inCategory(ProvisionCategory c) const;
  /** Adds or replaces (an empty id gets a new one). Returns the id. */
  QString save(const ProvisionItem& item);
  void remove(const QString& id);
  /** Takes `quantity` (logged as use). */
  void take(const QString& id, double quantity, const QDate& date);
  /** Stock count: the difference to the counted quantity is logged as use
   *  when less is left, else taken as a restock. */
  void count(const QString& id, double counted, const QDate& date);
  void clear();
  /** A typical stock for two on a week's cruise (demo, tests). */
  void loadExamples(const QDate& today);

  // Trip plan for the check.
  int crew() const { return m_crew; }
  int tripDays() const { return m_tripDays; }
  void setTrip(int crew, int days);

  /** Use per day: measured over the last 14 days if the item was used,
   *  else planned per person × crew. 0 if unknown. */
  double usePerDay(const ProvisionItem& item, const QDate& today) const;
  /** Measured use per day over `days` days (0 if nothing logged). */
  static double measuredPerDay(const ProvisionItem& item, const QDate& today,
                               int days = 14);
  /** Days until used up at the current use; -1 if unknown. */
  double daysLeft(const ProvisionItem& item, const QDate& today) const;
  QVector<ProvisionIssue> check(const QDate& today) const;
  QVector<ShoppingEntry> shoppingList(const QDate& today) const;
  CategorySummary summary(ProvisionCategory c, const QDate& today) const;

  static QString categoryName(ProvisionCategory c);
  static QString categoryIcon(ProvisionCategory c);
  static QVector<ProvisionCategory> categories();
  /** Typical shelf life when no best-before date is entered. */
  static int shelfLifeDays(ProvisionCategory c);
  void reload();

signals:
  void changed();

private:
  ProvisionStore();
  void load();
  void store() const;
  static QString file();
  QVector<ProvisionItem> m_items;
  int m_crew = 2;
  int m_tripDays = 7;
};
