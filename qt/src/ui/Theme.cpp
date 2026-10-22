#include "ui/Theme.h"

#include <QApplication>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QStandardPaths>
#include <QTime>
#include <QTimer>

#include "core/AppSettings.h"

namespace {
/** Stylesheets cannot reference in-memory images, so the combo box arrow is
 *  written to a temporary file. */
QString chevronFile(const QColor& color) {
  const QString path =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QStringLiteral("/opencpn-qt-chevron-%1.png").arg(color.rgba(), 8, 16);
  if (!QFileInfo::exists(path)) {
    QPixmap pm(56, 56);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(
        QPolygonF({QPointF(14, 22), QPointF(28, 36), QPointF(42, 22)}));
    p.end();
    pm.save(path);
  }
  return path;
}

/** White check mark for checked check boxes. */
QString checkFile() {
  const QString path =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QStringLiteral("/opencpn-qt-check.png");
  if (!QFileInfo::exists(path)) {
    QPixmap pm(56, 56);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::white, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(
        QPolygonF({QPointF(13, 29), QPointF(24, 40), QPointF(43, 17)}));
    p.end();
    pm.save(path);
  }
  return path;
}

QString rgba(const QColor& c) {
  return QStringLiteral("rgba(%1,%2,%3,%4)")
      .arg(c.red())
      .arg(c.green())
      .arg(c.blue())
      .arg(c.alphaF(), 0, 'f', 3);
}

Palette nightPalette() {
  Palette p;
  p.dark = true;
  p.water = QColor("#0a0f15");
  p.waterGlow = QColor("#101a26");
  p.land = QColor("#1f252d");
  p.landEdge = QColor("#2c343e");
  p.grid = QColor(255, 255, 255, 12);
  p.gridText = QColor(255, 255, 255, 70);
  p.panel = QColor(20, 23, 28, 236);
  p.panelBorder = QColor(255, 255, 255, 18);
  p.panelHover = QColor(255, 255, 255, 16);
  p.panelPressed = QColor(255, 255, 255, 30);
  p.shadow = QColor(0, 0, 0, 90);
  p.text = QColor("#f3f5f8");
  p.textDim = QColor("#9aa3ae");
  p.textFaint = QColor("#5b636d");
  p.accent = QColor("#3e6ae1");
  p.accentBright = QColor("#5d8bff");
  p.accentSoft = QColor(62, 106, 225, 60);
  p.route = QColor("#4d8dff");
  p.routeInactive = QColor("#7d8794");
  p.ownShip = QColor("#5d8bff");
  p.danger = QColor("#ff453a");
  p.warning = QColor("#ffb020");
  p.success = QColor("#30d158");
  p.ais = QColor("#c5ccd5");
  p.aisBorder = QColor("#0a0f15");
  p.dock = QColor(12, 14, 18, 245);
  return p;
}

Palette dayPalette() {
  Palette p;
  p.dark = false;
  p.water = QColor("#d9e4ee");
  p.waterGlow = QColor("#e6eef5");
  p.land = QColor("#f7f6f3");
  p.landEdge = QColor("#d6d2c8");
  p.grid = QColor(0, 0, 0, 14);
  p.gridText = QColor(0, 0, 0, 90);
  p.panel = QColor(255, 255, 255, 242);
  p.panelBorder = QColor(0, 0, 0, 14);
  p.panelHover = QColor(0, 0, 0, 10);
  p.panelPressed = QColor(0, 0, 0, 22);
  p.shadow = QColor(20, 30, 50, 40);
  p.text = QColor("#14171c");
  p.textDim = QColor("#5b6470");
  p.textFaint = QColor("#9aa1aa");
  p.accent = QColor("#3e6ae1");
  p.accentBright = QColor("#2f5bd3");
  p.accentSoft = QColor(62, 106, 225, 40);
  p.route = QColor("#2f6bff");
  p.routeInactive = QColor("#8a929c");
  p.ownShip = QColor("#2f5bd3");
  p.danger = QColor("#e5302a");
  p.warning = QColor("#e08a00");
  p.success = QColor("#1fa64a");
  p.ais = QColor("#4b545f");
  p.aisBorder = QColor("#ffffff");
  p.dock = QColor(250, 250, 251, 248);
  return p;
}
}  // namespace

Theme& Theme::instance() {
  static Theme t;
  return t;
}

Theme::Theme() : m_palette(nightPalette()) {
  connect(&AppSettings::instance(), &AppSettings::changed, this, &Theme::apply);
  // Re-check the automatic day/night switch every minute.
  auto* timer = new QTimer(this);
  timer->setInterval(60 * 1000);
  connect(timer, &QTimer::timeout, this, [this] {
    if (AppSettings::instance().theme == AppSettings::ThemeMode::Auto) apply();
  });
  timer->start();
}

void Theme::apply() {
  bool dark = true;
  switch (AppSettings::instance().theme) {
    case AppSettings::ThemeMode::Day:
      dark = false;
      break;
    case AppSettings::ThemeMode::Auto: {
      const int h = QTime::currentTime().hour();
      dark = h < 7 || h >= 19;
      break;
    }
    default:
      break;
  }
  setDark(dark);
}

void Theme::setDark(bool dark) {
  const bool first = qApp->styleSheet().isEmpty();
  if (!first && dark == m_palette.dark) return;
  m_palette = dark ? nightPalette() : dayPalette();
  qApp->setStyleSheet(styleSheet());
  emit changed();
}

ChartStyle Theme::chartStyle() {
  const Palette& p = instance().m_palette;
  const auto& cfg = AppSettings::instance();
  ChartStyle s;
  s.dark = p.dark;
  s.shallowDepth = cfg.shallowDepth;
  s.safetyDepth = cfg.safetyDepth;
  s.deepDepth = cfg.deepDepth;
  s.land = p.land;
  s.landEdge = p.landEdge;
  if (p.dark) {
    s.coast = QColor("#56606c");
    s.depthDeep = p.water;
    s.depthMedium = QColor("#0d1824");
    s.depthShallow = QColor("#12243a");
    s.depthVeryShallow = QColor("#183252");
    s.depthIntertidal = QColor("#1c2a24");
    s.contour = QColor(120, 150, 180, 90);
    s.safetyContour = QColor("#5d8bff");
    s.soundingShallow = QColor("#dfe6ee");
    s.soundingDeep = QColor("#6c7884");
    s.text = QColor("#c5ccd5");
    s.textHalo = p.water;
    s.magenta = QColor("#c25bc0");
    s.red = QColor("#e0483e");
    s.green = QColor("#38b55a");
    s.yellow = QColor("#d8b52c");
    s.white = QColor("#d8dde4");
    s.black = QColor("#15181c");
    s.grey = QColor("#7d8794");
    s.built = QColor("#252b33");
    s.structure = QColor("#2e353f");
  } else {
    s.coast = QColor("#8d8a80");
    s.depthDeep = QColor("#f3f7fa");
    s.depthMedium = QColor("#dce9f3");
    s.depthShallow = QColor("#b9d6ee");
    s.depthVeryShallow = QColor("#8fbfe6");
    s.depthIntertidal = QColor("#b5ccae");
    s.contour = QColor(90, 120, 150, 120);
    s.safetyContour = QColor("#2f5bd3");
    s.soundingShallow = QColor("#14171c");
    s.soundingDeep = QColor("#7d8590");
    s.text = QColor("#3a4048");
    s.textHalo = QColor(255, 255, 255);
    s.magenta = QColor("#b0309f");
    s.red = QColor("#d8282a");
    s.green = QColor("#14984a");
    s.yellow = QColor("#f2c618");
    s.white = QColor("#ffffff");
    s.black = QColor("#1b1d20");
    s.grey = QColor("#8a929c");
    s.built = QColor("#ebe7df");
    s.structure = QColor("#d9d4c8");
  }
  return s;
}

QFont Theme::font(qreal px, QFont::Weight weight) {
  QFont f;
  f.setFamilies({"Inter", "SF Pro Display", "Segoe UI Variable", "Segoe UI",
                 "Noto Sans", "Roboto", "Helvetica Neue", "Arial"});
  f.setPixelSize(int(px));
  f.setWeight(weight);
  f.setStyleStrategy(QFont::PreferAntialias);
  return f;
}

QFont Theme::numberFont(qreal px, QFont::Weight weight) {
  QFont f = font(px, weight);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
  f.setFeature("tnum", 1);  // tabular figures, no jumping digits
#endif
  f.setLetterSpacing(QFont::PercentageSpacing, 98);
  return f;
}

QString Theme::styleSheet() const {
  const Palette& p = m_palette;
  QString s = R"(
* { font-size: 14px; }
QWidget { color: @text; }
QLabel { background: transparent; }
QLabel[role="title"] { font-size: 22px; font-weight: 600; }
QLabel[role="section"] { color: @faint; font-size: 11px; font-weight: 600;
  letter-spacing: 1px; padding-top: 10px; }
QLabel[role="dim"] { color: @dim; font-size: 13px; }
QLabel[role="value"] { font-size: 15px; font-weight: 500; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QPushButton {
  background: @hover; color: @text; border: none; border-radius: 12px;
  padding: 10px 16px; font-weight: 600;
}
QPushButton:hover { background: @pressed; }
QPushButton:pressed { background: @pressed; }
QPushButton:disabled { color: @faint; }
QPushButton[role="primary"] { background: @accent; color: white; }
QPushButton[role="primary"]:hover { background: @accentBright; }
QPushButton[role="danger"] { background: @danger; color: white; }
QPushButton[role="segment"] { padding: 8px 6px; font-size: 13px; border-radius: 10px; }
QPushButton[role="segment"]:checked { background: @accent; color: white; }
QSlider::groove:horizontal { height: 6px; background: @pressed; border-radius: 3px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 3px; }
QSlider::handle:horizontal { background: white; width: 20px; height: 20px;
  margin: -7px 0; border-radius: 10px; }
QCheckBox { spacing: 8px; background: transparent; }
QCheckBox::indicator { width: 20px; height: 20px; border-radius: 6px;
  background: @hover; border: 1px solid @border; }
QCheckBox::indicator:checked { background: @accent; border: 1px solid @accent;
  image: url(@check); }
QPushButton[role="ghost"] { background: transparent; color: @dim; padding: 8px 10px; }
QPushButton[role="ghost"]:hover { background: @hover; color: @text; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QDateTimeEdit {
  background: @hover; border: 1px solid transparent; border-radius: 10px;
  padding: 8px 10px; color: @text; selection-background-color: @accent;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus,
QDateTimeEdit:focus {
  border: 1px solid @accent;
}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }
QComboBox::drop-down, QDateTimeEdit::drop-down { border: none; width: 26px; }
QDateTimeEdit::down-arrow { image: url(@chevron); width: 14px; height: 14px; }
QCalendarWidget QWidget { background: @panelSolid; color: @text;
  alternate-background-color: @hover; }
QCalendarWidget QToolButton { background: transparent; color: @text; padding: 6px; }
QCalendarWidget QAbstractItemView:disabled { color: @faint; }
QCalendarWidget QAbstractItemView { selection-background-color: @accent; }
QComboBox::down-arrow { image: url(@chevron); width: 14px; height: 14px; }
QComboBox QAbstractItemView {
  background: @panelSolid; border: 1px solid @border; border-radius: 10px;
  padding: 6px; outline: none; selection-background-color: @accentSoft;
}
QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 2px; }
QScrollBar::handle:vertical { background: @pressed; border-radius: 3px; min-height: 40px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QScrollBar:horizontal { height: 0; }
QMenu {
  background: @panelSolid; border: 1px solid @border; border-radius: 14px;
  padding: 8px;
}
QMenu::item { padding: 10px 18px; border-radius: 9px; }
QMenu::item:selected { background: @accentSoft; }
QMenu::separator { height: 1px; background: @border; margin: 6px 8px; }
QToolTip {
  background: @panelSolid; color: @text; border: 1px solid @border;
  border-radius: 8px; padding: 6px 10px;
}
QMessageBox, QDialog { background: @panelSolid; }
)";
  QColor solid = p.panel;
  solid.setAlpha(255);
  s.replace("@text", rgba(p.text))
      .replace("@dim", rgba(p.textDim))
      .replace("@faint", rgba(p.textFaint))
      .replace("@hover", rgba(p.panelHover))
      .replace("@pressed", rgba(p.panelPressed))
      .replace("@accentBright", rgba(p.accentBright))
      .replace("@accentSoft", rgba(p.accentSoft))
      .replace("@accent", rgba(p.accent))
      .replace("@danger", rgba(p.danger))
      .replace("@panelSolid", rgba(solid))
      .replace("@border", rgba(p.panelBorder))
      .replace("@chevron", chevronFile(p.textDim))
      .replace("@check", checkFile());
  return s;
}
