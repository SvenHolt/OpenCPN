// Content pages of the side drawer.
#pragma once

#include <QTimer>
#include <QWidget>

class ChartView;
class QComboBox;
class QLineEdit;
class QLabel;
class QSpinBox;
class QVBoxLayout;
class QStackedWidget;

/** Hoverable, clickable rounded row used by all lists. */
class ListRow : public QWidget {
  Q_OBJECT
public:
  explicit ListRow(QWidget* parent = nullptr);
  void setHighlight(const QColor& c) {
    m_highlight = c;
    update();
  }

signals:
  void clicked();

protected:
  void paintEvent(QPaintEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void enterEvent(QEnterEvent*) override { update(); }
  void leaveEvent(QEvent*) override { update(); }

private:
  QColor m_highlight;
};

/** Base class: scrollable list below an optional action bar. */
class ListPage : public QWidget {
  Q_OBJECT
public:
  explicit ListPage(QWidget* parent = nullptr);

protected:
  void clearRows();
  void addRow(QWidget* row);
  void setEmptyText(const QString& text);
  QVBoxLayout* actions() const { return m_actions; }

private:
  QVBoxLayout* m_actions;
  QVBoxLayout* m_rows;
  QLabel* m_empty;
};

class RoutesPage : public ListPage {
  Q_OBJECT
public:
  explicit RoutesPage(ChartView* chart, QWidget* parent = nullptr);

signals:
  void newRouteRequested();

private:
  void rebuild();
  void importGpx();
  void exportGpx();
  ChartView* m_chart;
};

class MarksPage : public ListPage {
  Q_OBJECT
public:
  explicit MarksPage(ChartView* chart, QWidget* parent = nullptr);

private:
  void rebuild();
  ChartView* m_chart;
  QTimer m_refresh;
};

class AisPage : public ListPage {
  Q_OBJECT
public:
  explicit AisPage(ChartView* chart, QWidget* parent = nullptr);

signals:
  void targetSelected(quint32 mmsi);

private:
  void rebuild();
  ChartView* m_chart;
  QTimer m_refresh;
  QLabel* m_summary;
};

class ConnectionsPage : public ListPage {
  Q_OBJECT
public:
  explicit ConnectionsPage(QWidget* parent = nullptr);

private:
  void rebuild();
  void updateForm();
  QComboBox* m_type;
  QLineEdit* m_host;
  QSpinBox* m_port;
  QComboBox* m_serial;
  QComboBox* m_baud;
  QWidget* m_hostRow;
  QWidget* m_portRow;
  QWidget* m_serialRow;
  QWidget* m_baudRow;
  QLabel* m_monitor;
  QStringList m_lastSentences;
};

class ChartsPage : public ListPage {
  Q_OBJECT
public:
  explicit ChartsPage(ChartView* chart, QWidget* parent = nullptr);

private:
  void rebuild();
  ChartView* m_chart;
  QLabel* m_status;
};

class SettingsPage : public QWidget {
  Q_OBJECT
public:
  explicit SettingsPage(QWidget* parent = nullptr);
};
