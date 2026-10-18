// Polar data: diagram, import of polar files and a table editor.
#pragma once

#include <QDialog>
#include <QWidget>

#include "core/Polar.h"

class QLabel;
class QTableWidget;

/** Half-circle polar diagram with one curve per wind speed. */
class PolarDiagram : public QWidget {
  Q_OBJECT
public:
  explicit PolarDiagram(QWidget* parent = nullptr);
  void setPolar(const Polar& p);
  QSize sizeHint() const override { return {360, 400}; }

protected:
  void paintEvent(QPaintEvent*) override;

private:
  Polar m_polar;
};

/** Table editor: wind angles as rows, wind speeds as columns. */
class PolarEditor : public QDialog {
  Q_OBJECT
public:
  explicit PolarEditor(const Polar& p, QWidget* parent = nullptr);
  /** The edited polar; false with a message if the table is invalid. */
  bool result(Polar* out, QString* error) const;

private:
  void fill(const Polar& p);
  QTableWidget* m_table;
  QString m_name;
};

/** Drawer section: current polar, diagram and actions. */
class PolarPanel : public QWidget {
  Q_OBJECT
public:
  explicit PolarPanel(QWidget* parent = nullptr);

private:
  void sync();
  PolarDiagram* m_diagram;
  QLabel* m_name;
  QLabel* m_info;
};
