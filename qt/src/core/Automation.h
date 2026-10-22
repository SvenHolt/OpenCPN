// Automations for the on-board systems: a named list of actions (heater,
// room targets, hot water, light scenes, single lights and consumers) that
// runs on a tap or at a time of day, optionally limited to weekdays and
// months ("winter shower": heater on, saloon 21 °C, heads 23 °C, hot water
// on, shower lights).
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTime>
#include <QTimer>
#include <QVector>

struct AutomationAction {
  enum class Type {
    Heater,        // on
    ZoneSetpoint,  // target = zone id, value = °C
    WaterHeater,   // on
    Scene,         // target = scene id
    Light,         // target = light id, on
    Consumer,      // target = consumer id, on
  };
  Type type = Type::Heater;
  QString target;
  double value = 20;
  bool on = true;

  /** Human readable, e.g. "Heads 23.0 °C". */
  QString describe() const;
  QJsonObject toJson() const;
  static AutomationAction fromJson(const QJsonObject& o);
  static QString typeName(Type t);
};

struct Automation {
  QString id, name, icon = "auto";
  bool enabled = true;
  enum class Trigger { Manual, Time } trigger = Trigger::Manual;
  QTime time = QTime(7, 0);
  int weekdays = 0x7f;  // bit 0 = Monday … bit 6 = Sunday
  int months = 0xfff;   // bit 0 = January … bit 11 = December
  QDateTime lastRun;
  QVector<AutomationAction> actions;

  /** Allowed on this date (weekday and month filter). */
  bool allowedOn(const QDate& d) const;
  /** Short summary of trigger and filters, e.g. "Daily 07:00 · Nov–Mar". */
  QString schedule() const;
  QJsonObject toJson() const;
  static Automation fromJson(const QJsonObject& o);

  static constexpr int kWinter = 0b1110'0000'0111;  // Nov … Mar
};

class AutomationStore : public QObject {
  Q_OBJECT
public:
  static AutomationStore& instance();

  const QVector<Automation>& automations() const { return m_items; }
  const Automation* find(const QString& id) const;
  /** Adds or replaces (by id; a new id is made for an empty one). Returns
   *  the id. */
  QString save(const Automation& a);
  void remove(const QString& id);
  /** Runs the actions now (manual trigger or schedule). */
  void run(const QString& id);
  static void execute(const Automation& a);
  /** Runs the scheduled automations due at `now` (once per day, up to 10
   *  minutes late). Returns the ids that ran. */
  QStringList check(const QDateTime& now);
  static QVector<Automation> defaults();
  /** Reloads from disk (tests). */
  void reload();

signals:
  void changed();
  void ran(const QString& id, const QString& name);

private:
  AutomationStore();
  void load();
  void store() const;
  void runAt(const QString& id, const QDateTime& when);
  static QString file();
  QVector<Automation> m_items;
  QTimer m_timer;
};
