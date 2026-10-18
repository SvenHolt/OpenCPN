// Supplies: range analysis (water, holding tank, battery, diesel at anchor
// and underway) and provisions (stock by category, item editor,
// consumption check and shopping list).
#pragma once

#include <QTimer>
#include <QWidget>

#include "core/Provisions.h"
#include "core/Range.h"

class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
class QCheckBox;

/** Remaining water, holding tank space, battery and diesel day by day,
 *  with the temperature below. */
class RangeChart : public QWidget {
  Q_OBJECT
public:
  explicit RangeChart(QWidget* parent = nullptr);
  void setResult(const RangeResult& r, const RangeState& s);
  QSize sizeHint() const override { return {360, 210}; }

protected:
  void paintEvent(QPaintEvent*) override;

private:
  RangeResult m_result;
  RangeState m_state;
};

class RangePage : public QWidget {
  Q_OBJECT
public:
  explicit RangePage(QWidget* parent = nullptr);
  /** Recomputes from the log, the boat and the forecast. */
  void refresh();
  BoatMode mode() const { return m_mode; }
  void setMode(BoatMode m);
  void setWindow(RangeWindow w);
  const RangeResult& result() const { return m_result; }

protected:
  void showEvent(QShowEvent*) override;

private:
  void updateForecast();
  BoatMode m_mode = BoatMode::Anchor;
  RangeWindow m_window = RangeWindow::Days7;
  RangeResult m_result;
  QComboBox* m_windowBox;
  QPushButton* m_modes[2];
  QLabel* m_summary;
  QLabel* m_basis;
  QLabel* m_weather;
  QWidget* m_rows[4];
  RangeChart* m_chart;
  QTimer m_refresh;
};

/** Compact range for the boat view: anchor and underway. */
class RangeCard : public QWidget {
  Q_OBJECT
public:
  explicit RangeCard(QWidget* parent = nullptr);
  QSize sizeHint() const override { return {380, 120}; }
  void refresh();

protected:
  void paintEvent(QPaintEvent*) override;

private:
  RangeResult m_anchor, m_underway;
  QTimer m_timer;
};

/** Edits one provision item, with stock count and use log. */
class ProvisionEditor : public QWidget {
  Q_OBJECT
public:
  explicit ProvisionEditor(QWidget* parent = nullptr);
  void edit(const ProvisionItem& item);
  ProvisionItem item() const;

signals:
  void closed();

private:
  ProvisionItem m_item;
  QLineEdit* m_name;
  QComboBox* m_category;
  QDoubleSpinBox* m_quantity;
  QComboBox* m_unit;
  QCheckBox* m_hasDate;
  QDateEdit* m_bestBefore;
  QDoubleSpinBox* m_planned;
  QDoubleSpinBox* m_min;
  QDoubleSpinBox* m_counted;
  QLabel* m_usage;
  QPushButton* m_delete;
  QWidget* m_countBox;
};

class ProvisionsPage : public QWidget {
  Q_OBJECT
public:
  explicit ProvisionsPage(QWidget* parent = nullptr);
  ProvisionEditor* editor() const { return m_editor; }
  /** -1: all categories. */
  void setCategory(int category);
  int category() const { return m_category; }
  /** Opens the editor (empty id: new item in the current category). */
  void editItem(const QString& id);

private:
  void rebuild();
  int m_category = -1;
  QStackedWidget* m_stack;
  QVBoxLayout* m_list;
  QVector<QPushButton*> m_chips;
  QLabel* m_totals;
  ProvisionEditor* m_editor;
};

class ProvisionCheckPage : public QWidget {
  Q_OBJECT
public:
  explicit ProvisionCheckPage(QWidget* parent = nullptr);

signals:
  void itemRequested(const QString& id);

private:
  void rebuild();
  QSpinBox* m_crew;
  QSpinBox* m_days;
  QVBoxLayout* m_content;
};
