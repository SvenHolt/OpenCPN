#include "ui/SideDrawer.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

SideDrawer::SideDrawer(QWidget* parent)
    : GlassPanel(parent, 28), m_anim(this, "pos") {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(kShadow + 22, kShadow + 18, kShadow + 14, kShadow + 18);
  l->setSpacing(12);
  auto* header = new QHBoxLayout;
  m_back = new IconButton("back", tr("Back"), this);
  m_back->hide();
  connect(m_back, &QAbstractButton::clicked, this, [this] {
    const int parent = parentOf(m_stack->currentIndex());
    if (parent >= 0) open(parent);
  });
  header->addWidget(m_back);
  m_title = new QLabel(this);
  m_title->setProperty("role", "title");
  header->addWidget(m_title, 1);
  auto* close = new IconButton("close", tr("Close"), this);
  connect(close, &QAbstractButton::clicked, this, &SideDrawer::close);
  header->addWidget(close);
  l->addLayout(header);
  m_stack = new QStackedWidget(this);
  l->addWidget(m_stack, 1);

  m_anim.setDuration(260);
  m_anim.setEasingCurve(QEasingCurve::OutCubic);
  connect(&m_anim, &QPropertyAnimation::finished, this, [this] {
    if (!m_open) hide();
  });
  hide();
}

int SideDrawer::addPage(const QString& title, QWidget* page, int parent) {
  m_titles << title;
  m_parents << parent;
  return m_stack->addWidget(page);
}

int SideDrawer::rootOf(int page) const {
  int guard = 0;
  while (parentOf(page) >= 0 && ++guard < 16) page = parentOf(page);
  return page;
}

void SideDrawer::toggle(int page) {
  // On a sub-page the main menu button leads back to its section.
  if (m_open && m_stack->currentIndex() == page)
    close();
  else
    open(page);
}

void SideDrawer::open(int page) {
  m_stack->setCurrentIndex(page);
  m_title->setText(m_titles.value(page));
  m_back->setVisible(parentOf(page) >= 0);
  if (m_open) {
    emit pageChanged(page);
    return;
  }
  m_open = true;
  emit pageChanged(page);
  show();
  raise();
  m_anim.stop();
  m_anim.setStartValue(QPoint(m_area.left() - width(), m_area.top()));
  m_anim.setEndValue(m_area.topLeft());
  m_anim.start();
}

void SideDrawer::close() {
  if (!m_open) return;
  m_open = false;
  emit pageChanged(-1);
  m_anim.stop();
  m_anim.setStartValue(pos());
  m_anim.setEndValue(QPoint(m_area.left() - width(), m_area.top()));
  m_anim.start();
}

void SideDrawer::place(const QRect& area) {
  m_area = QRect(area.topLeft(), QSize(kWidth + 2 * kShadow, area.height()));
  resize(m_area.size());
  if (m_anim.state() != QAbstractAnimation::Running)
    move(m_open ? m_area.topLeft()
                : QPoint(m_area.left() - width(), m_area.top()));
}
