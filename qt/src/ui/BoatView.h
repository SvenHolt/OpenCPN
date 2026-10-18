// Boat systems view: top-down cabin layout (Dufour 2800 style) with room
// temperatures, lights, tanks, battery, heater, solar and shore power, plus
// cards for the Victron energy system, the Webasto heater, tanks, light
// scenes and consumers.
#pragma once

#include <QHash>
#include <QPainterPath>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QButtonGroup;

/** The painted cabin layout. Tapping a light switches it. */
class CabinLayout : public QWidget {
  Q_OBJECT
public:
  explicit CabinLayout(QWidget* parent = nullptr);
  QSize minimumSizeHint() const override { return {420, 240}; }
  /** Screen position of a light (for tests). */
  QPointF lightPos(const QString& id) const;

protected:
  void paintEvent(QPaintEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void resizeEvent(QResizeEvent*) override;

private:
  /** Boat coordinates: x 0 = transom … 1 = bow, y -1 = port … +1 = stbd. */
  QPointF map(double x, double y) const;
  QRectF boatRect(double x0, double y0, double x1, double y1) const;
  void drawRoom(QPainter& p, const QString& zone, const QRectF& r,
                const QPointF& labelAt);
  void drawFurniture(QPainter& p);
  void drawEquipment(QPainter& p);
  void drawLights(QPainter& p);

  QRectF m_area;  // hull bounding box on screen
  QPainterPath m_hull;
  QHash<QString, QPointF> m_lightPos;  // boat coordinates
  QRectF m_shoreHit;
};

/** Energy card: battery ring and the flows of solar, shore power and loads. */
class EnergyCard : public QWidget {
  Q_OBJECT
public:
  explicit EnergyCard(QWidget* parent = nullptr);
  QSize sizeHint() const override { return {380, 250}; }

protected:
  void paintEvent(QPaintEvent*) override;
};

/** Tank levels as vertical bars. */
class TanksCard : public QWidget {
  Q_OBJECT
public:
  explicit TanksCard(QWidget* parent = nullptr);
  QSize sizeHint() const override { return {380, 150}; }

protected:
  void paintEvent(QPaintEvent*) override;
};

class BoatView : public QWidget {
  Q_OBJECT
public:
  explicit BoatView(QWidget* parent = nullptr);
  void setLeftInset(int px);

signals:
  void editScenesRequested();
  void automationsRequested();
  void rangeRequested();

protected:
  void paintEvent(QPaintEvent*) override;

private:
  void sync();
  void rebuildScenes();
  void rebuildAutomations();
  QWidget* card(const QString& title, QWidget* content,
                QWidget* action = nullptr);
  class QGridLayout* m_sceneRow;
  QVBoxLayout* m_automations;
  QLabel* m_zones;
  QLabel* m_water;
  class ToggleSwitch* m_waterOn;
  QWidget* m_left;
  CabinLayout* m_layout;
  QButtonGroup* m_scenes;
  QLabel* m_heaterState;
  QLabel* m_heaterValues;
  QLabel* m_setpoint;
  QPushButton* m_heaterOn;
  QVBoxLayout* m_consumers;
  QHash<QString, QWidget*> m_consumerSwitch;
};
