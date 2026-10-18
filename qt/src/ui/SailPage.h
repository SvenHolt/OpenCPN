// Drawer page to enter the sail plan, propulsion and the fuel tank setup.
#pragma once

#include <QWidget>

class QButtonGroup;
class QDoubleSpinBox;
class QLabel;
class QSlider;

class SailPage : public QWidget {
  Q_OBJECT
public:
  explicit SailPage(QWidget* parent = nullptr);

private:
  void apply();
  void sync();

  QButtonGroup* m_propulsion;
  QButtonGroup* m_main;
  QButtonGroup* m_head;
  QButtonGroup* m_downwind;
  QSlider* m_furl;
  QLabel* m_furlLabel;
  QWidget* m_furlRow;
  QDoubleSpinBox* m_tank;
  QDoubleSpinBox* m_consumption;
  QDoubleSpinBox* m_cruise;
  QDoubleSpinBox* m_fuel;
  bool m_syncing = false;
};
