// Design tokens and global stylesheet of the Qt6 frontend. The look follows
// modern automotive infotainment systems: a full screen map, dark glass
// panels, a single accent color and large, calm typography.
#pragma once

#include <QColor>
#include <QFont>
#include <QObject>

struct Palette {
  bool dark = true;
  QColor water, waterGlow, land, landEdge;
  QColor grid, gridText;
  QColor panel, panelBorder, panelHover, panelPressed, shadow;
  QColor text, textDim, textFaint;
  QColor accent, accentBright, accentSoft;
  QColor route, routeInactive, ownShip;
  QColor danger, warning, success;
  QColor ais, aisBorder;
  QColor dock;
};

class Theme : public QObject {
  Q_OBJECT
public:
  static Theme& instance();
  static const Palette& p() { return instance().m_palette; }

  bool isDark() const { return m_palette.dark; }
  /** Re-evaluates the theme from the settings and applies it. */
  void apply();

  /** Font helpers: display fonts are light and large, labels are small caps. */
  static QFont font(qreal pixelSize, QFont::Weight weight = QFont::Normal);
  static QFont numberFont(qreal pixelSize, QFont::Weight weight = QFont::Light);

signals:
  void changed();

private:
  Theme();
  void setDark(bool dark);
  QString styleSheet() const;

  Palette m_palette;
};
