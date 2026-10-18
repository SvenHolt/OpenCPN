#include "ui/GlassPanel.h"

#include <QPainter>
#include <QPainterPath>

#include "ui/Icons.h"
#include "ui/Theme.h"

void paintGlass(QPainter& p, const QRectF& r, qreal radius, bool shadow) {
  const Palette& pal = Theme::p();
  p.setRenderHint(QPainter::Antialiasing);
  if (shadow) {
    // Cheap soft shadow: stacked, slightly offset translucent rounded rects.
    QColor s = pal.shadow;
    const int steps = 6;
    for (int i = steps; i >= 1; --i) {
      s.setAlphaF(pal.shadow.alphaF() * (1.0 - qreal(i) / (steps + 1)) / 3.0);
      p.setPen(Qt::NoPen);
      p.setBrush(s);
      const qreal g = i * 2.0;
      p.drawRoundedRect(r.adjusted(-g, -g + 3, g, g + 3), radius + g,
                        radius + g);
    }
  }
  p.setBrush(pal.panel);
  p.setPen(QPen(pal.panelBorder, 1));
  p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

GlassPanel::GlassPanel(QWidget* parent, int radius)
    : QWidget(parent), m_radius(radius) {
  setAttribute(Qt::WA_StyledBackground, false);
  setContentsMargins(kShadow, kShadow, kShadow, kShadow);
  connect(&Theme::instance(), &Theme::changed, this,
          qOverload<>(&QWidget::update));
}

QRectF GlassPanel::panelRect() const {
  return QRectF(rect()).adjusted(kShadow, kShadow, -kShadow, -kShadow);
}

void GlassPanel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  paintGlass(p, panelRect(), m_radius);
}

RoundButton::RoundButton(const QString& icon, QWidget* parent, int size)
    : QAbstractButton(parent), m_icon(icon), m_size(size) {
  setCursor(Qt::PointingHandCursor);
  setFixedSize(size, size);
  connect(&Theme::instance(), &Theme::changed, this,
          qOverload<>(&QWidget::update));
}

void RoundButton::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = QRectF(rect()).adjusted(4, 4, -4, -4);
  const bool accent = isChecked() && m_accentChecked;
  if (accent) {
    p.setPen(Qt::NoPen);
    p.setBrush(pal.accent);
    p.drawEllipse(r);
  } else {
    paintGlass(p, r, r.width() / 2, true);
  }
  if (isDown() || underMouse()) {
    p.setPen(Qt::NoPen);
    p.setBrush(isDown() ? pal.panelPressed : pal.panelHover);
    p.drawEllipse(r);
  }
  const qreal is = r.width() * 0.46;
  p.translate(r.center());
  p.rotate(m_rotation);
  Icons::paint(&p, m_icon, QRectF(-is / 2, -is / 2, is, is),
               accent ? QColor(Qt::white) : pal.text);
}

DockButton::DockButton(const QString& icon, const QString& label,
                       QWidget* parent)
    : QAbstractButton(parent), m_icon(icon) {
  setText(label);
  setToolTip(label);
  setCursor(Qt::PointingHandCursor);
  setMinimumSize(64, 56);
  connect(&Theme::instance(), &Theme::changed, this,
          qOverload<>(&QWidget::update));
}

void DockButton::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = QRectF(rect()).adjusted(3, 3, -3, -3);
  if (m_danger) {
    p.setPen(Qt::NoPen);
    p.setBrush(isDown() ? pal.danger.darker(120) : pal.danger);
    p.drawRoundedRect(r, 14, 14);
  } else if (isChecked() || isDown() || underMouse()) {
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? pal.panelPressed : pal.panelHover);
    p.drawRoundedRect(r, 14, 14);
  }
  const QColor fg = m_danger      ? QColor(Qt::white)
                    : isChecked() ? pal.accentBright
                                  : pal.text;
  const qreal is = 24;
  Icons::paint(&p, m_icon, QRectF(r.center().x() - is / 2, r.top() + 7, is, is),
               fg);
  p.setFont(Theme::font(11, QFont::DemiBold));
  p.setPen(m_danger      ? QColor(Qt::white)
           : isChecked() ? pal.accentBright
                         : pal.textDim);
  p.drawText(QRectF(r.left(), r.top() + 34, r.width(), 16), Qt::AlignCenter,
             text());
  if (isChecked() && !m_danger) {
    p.setPen(Qt::NoPen);
    p.setBrush(pal.accentBright);
    p.drawRoundedRect(QRectF(r.center().x() - 10, r.bottom() - 2, 20, 3), 1.5,
                      1.5);
  }
}

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
  setCheckable(true);
  setCursor(Qt::PointingHandCursor);
  setFixedSize(sizeHint());
  m_anim.setDuration(160);
  m_anim.setEasingCurve(QEasingCurve::OutCubic);
  connect(&m_anim, &QVariantAnimation::valueChanged, this,
          [this](const QVariant& v) {
            m_pos = v.toReal();
            update();
          });
  connect(this, &QAbstractButton::toggled, this, [this](bool on) {
    m_anim.stop();
    m_anim.setStartValue(m_pos);
    m_anim.setEndValue(on ? 1.0 : 0.0);
    m_anim.start();
  });
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  if (m_anim.state() != QAbstractAnimation::Running)
    m_pos = isChecked() ? 1.0 : 0.0;
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = QRectF(rect()).adjusted(1, 3, -1, -3);
  QColor off = pal.panelPressed;
  QColor on = pal.accent;
  QColor track(int(off.red() + (on.red() - off.red()) * m_pos),
               int(off.green() + (on.green() - off.green()) * m_pos),
               int(off.blue() + (on.blue() - off.blue()) * m_pos),
               int(off.alpha() + (on.alpha() - off.alpha()) * m_pos));
  p.setPen(Qt::NoPen);
  p.setBrush(track);
  p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
  const qreal d = r.height() - 4;
  const qreal x = r.left() + 2 + (r.width() - d - 4) * m_pos;
  p.setBrush(QColor(0, 0, 0, 40));
  p.drawEllipse(QRectF(x, r.top() + 3, d, d));
  p.setBrush(Qt::white);
  p.drawEllipse(QRectF(x, r.top() + 2, d, d));
}

IconButton::IconButton(const QString& icon, const QString& tooltip,
                       QWidget* parent, bool danger)
    : QAbstractButton(parent), m_icon(icon), m_danger(danger) {
  setToolTip(tooltip);
  setCursor(Qt::PointingHandCursor);
  setFixedSize(sizeHint());
}

void IconButton::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  if (underMouse() || isDown() || isChecked()) {
    p.setPen(Qt::NoPen);
    p.setBrush(isDown() ? pal.panelPressed : pal.panelHover);
    p.drawRoundedRect(QRectF(rect()), 10, 10);
  }
  QColor c = m_danger && underMouse() ? pal.danger
             : isChecked()            ? pal.accentBright
                                      : pal.textDim;
  if (underMouse() && !m_danger) c = pal.text;
  Icons::paint(&p, m_icon, QRectF(rect()).adjusted(8, 8, -8, -8), c);
}
