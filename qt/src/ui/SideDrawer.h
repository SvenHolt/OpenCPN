// Slide-in panel on the left that hosts the app sections (routes, marks,
// AIS, weather, settings). Pages can have a parent page (the settings
// sub-pages); those show a back button in the header.
#pragma once

#include <QPropertyAnimation>
#include <QStackedWidget>

#include "ui/GlassPanel.h"

class QLabel;

class SideDrawer : public GlassPanel {
  Q_OBJECT
public:
  explicit SideDrawer(QWidget* parent = nullptr);

  /** Adds a page; `parent` >= 0 makes it a sub-page of that page. */
  int addPage(const QString& title, QWidget* page, int parent = -1);
  int currentPage() const { return m_open ? m_stack->currentIndex() : -1; }
  int parentOf(int page) const { return m_parents.value(page, -1); }
  /** The top level page a page belongs to. */
  int rootOf(int page) const;
  int pageCount() const { return m_stack->count(); }
  QString title(int page) const { return m_titles.value(page); }
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
  IconButton* m_back;
  QStringList m_titles;
  QVector<int> m_parents;
  QPropertyAnimation m_anim;
  QRect m_area;
  bool m_open = false;
};
