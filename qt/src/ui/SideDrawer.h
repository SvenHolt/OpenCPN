// Slide-in panel on the left that hosts the app sections (routes, marks,
// AIS, data connections, settings).
#pragma once

#include <QPropertyAnimation>
#include <QStackedWidget>

#include "ui/GlassPanel.h"

class QLabel;

class SideDrawer : public GlassPanel {
  Q_OBJECT
public:
  explicit SideDrawer(QWidget* parent = nullptr);

  int addPage(const QString& title, QWidget* page);
  int currentPage() const { return m_open ? m_stack->currentIndex() : -1; }
  bool isOpen() const { return m_open; }

  /** Opens the page, or closes the drawer if it is already showing it. */
  void toggle(int page);
  void open(int page);
  void close();
  /** Positions the drawer for the given available area. */
  void place(const QRect& area);

  static constexpr int kWidth = 420;

signals:
  void pageChanged(int page);  // -1 when closed

private:
  QStackedWidget* m_stack;
  QLabel* m_title;
  QStringList m_titles;
  QPropertyAnimation m_anim;
  QRect m_area;
  bool m_open = false;
};
