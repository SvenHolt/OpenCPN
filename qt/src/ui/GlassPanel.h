// Basic building blocks of the automotive style UI: floating glass panels,
// round map buttons, toggle switches and dock buttons.
#pragma once

#include <QAbstractButton>
#include <QVariantAnimation>
#include <QWidget>

/** Rounded, translucent, softly shadowed panel floating above the chart. */
class GlassPanel : public QWidget {
  Q_OBJECT
public:
  explicit GlassPanel(QWidget* parent = nullptr, int radius = 22);
  void setRadius(int r) {
    m_radius = r;
    update();
  }
  /** Extra space reserved around the panel for the drop shadow. */
  static constexpr int kShadow = 14;

protected:
  void paintEvent(QPaintEvent*) override;
  QRectF panelRect() const;
  int m_radius;
};

/** Circular icon button used for the floating map controls. */
class RoundButton : public QAbstractButton {
  Q_OBJECT
public:
  RoundButton(const QString& icon, QWidget* parent = nullptr, int size = 52);
  void setIconName(const QString& name) {
    m_icon = name;
    update();
  }
  /** Rotation of the icon in degrees (used by the compass button). */
  void setIconRotation(qreal deg) {
    m_rotation = deg;
    update();
  }
  void setAccentWhenChecked(bool on) { m_accentChecked = on; }
  QSize sizeHint() const override { return {m_size, m_size}; }

protected:
  void paintEvent(QPaintEvent*) override;
  void enterEvent(QEnterEvent*) override { update(); }
  void leaveEvent(QEvent*) override { update(); }

private:
  QString m_icon;
  int m_size;
  qreal m_rotation = 0;
  bool m_accentChecked = true;
};

/** Icon + label button of the bottom dock. */
class DockButton : public QAbstractButton {
  Q_OBJECT
public:
  DockButton(const QString& icon, const QString& label,
             QWidget* parent = nullptr);
  void setIconName(const QString& n) {
    m_icon = n;
    update();
  }
  void setDanger(bool d) { m_danger = d; }
  QSize sizeHint() const override { return {76, 60}; }

protected:
  void paintEvent(QPaintEvent*) override;
  void enterEvent(QEnterEvent*) override { update(); }
  void leaveEvent(QEvent*) override { update(); }

private:
  QString m_icon;
  bool m_danger = false;
};

/** iOS/automotive style animated on/off switch. */
class ToggleSwitch : public QAbstractButton {
  Q_OBJECT
public:
  explicit ToggleSwitch(QWidget* parent = nullptr);
  QSize sizeHint() const override { return {50, 30}; }

protected:
  void paintEvent(QPaintEvent*) override;

private:
  QVariantAnimation m_anim;
  qreal m_pos = 0;
};

/** Small flat icon button for list rows. */
class IconButton : public QAbstractButton {
  Q_OBJECT
public:
  IconButton(const QString& icon, const QString& tooltip,
             QWidget* parent = nullptr, bool danger = false);
  void setIconName(const QString& n) {
    m_icon = n;
    update();
  }
  QSize sizeHint() const override { return {36, 36}; }

protected:
  void paintEvent(QPaintEvent*) override;
  void enterEvent(QEnterEvent*) override { update(); }
  void leaveEvent(QEvent*) override { update(); }

private:
  QString m_icon;
  bool m_danger;
};

/** Paints the standard panel background (used by non-GlassPanel widgets). */
void paintGlass(QPainter& p, const QRectF& r, qreal radius, bool shadow = true);
