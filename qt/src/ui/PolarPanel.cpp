#include "ui/PolarPanel.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>

#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
QLabel* label(const QString& text, const char* role = nullptr) {
  auto* l = new QLabel(text);
  if (role) l->setProperty("role", role);
  return l;
}

QPushButton* button(const QString& text, const QString& icon) {
  auto* b = new QPushButton(text);
  b->setCursor(Qt::PointingHandCursor);
  b->setIcon(Icons::icon(icon, Theme::p().text, 18));
  return b;
}
}  // namespace

// --------------------------------------------------------------------------

PolarDiagram::PolarDiagram(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  setFixedHeight(400);
}

void PolarDiagram::setPolar(const Polar& p) {
  m_polar = p;
  update();
}

void PolarDiagram::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  if (!m_polar.isValid()) return;
  // Starboard half: 0° up, 180° down, the boat at the left edge.
  const double maxV = std::ceil(m_polar.maxSpeed() + 0.5);
  const double r = std::min(width() - 30.0, (height() - 20) / 2.0);
  const QPointF c(14, height() / 2.0);
  const auto pos = [&](double twa, double v) {
    const double a = twa * M_PI / 180;
    return c + QPointF(std::sin(a), -std::cos(a)) * (v / maxV * r);
  };
  // Grid: speed rings and angle spokes.
  QPen grid(pal.grid, 1);
  p.setPen(grid);
  p.setFont(Theme::font(10));
  for (int v = 2; v <= maxV; v += 2) {
    const double rr = v / maxV * r;
    p.drawArc(QRectF(c.x() - rr, c.y() - rr, 2 * rr, 2 * rr), 90 * 16,
              -180 * 16);
    p.setPen(pal.textFaint);
    p.drawText(QPointF(c.x() + 3, c.y() - rr - 2), QString::number(v));
    p.setPen(grid);
  }
  for (int a = 30; a < 180; a += 30) {
    p.drawLine(c, pos(a, maxV));
    p.setPen(pal.textFaint);
    p.drawText(pos(a, maxV * 1.04) - QPointF(6, -4), QString::number(a));
    p.setPen(grid);
  }
  // Curves for a few wind speeds, light to strong.
  const QVector<double> winds{6, 10, 14, 20, 25};
  for (int k = 0; k < winds.size(); ++k) {
    const double w = winds[k];
    if (w > m_polar.tws.last() + 5) continue;
    QColor col = QColor::fromHsvF(0.58 - 0.11 * k, 0.75, 0.95);
    QPainterPath path;
    bool started = false;
    for (double a = 0; a <= 180; a += 2) {
      const double v = m_polar.speedAt(w, a);
      if (v <= 0) {
        started = false;
        continue;
      }
      if (!started) {
        path.moveTo(pos(a, v));
        started = true;
      } else {
        path.lineTo(pos(a, v));
      }
    }
    p.setPen(QPen(col, 2.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    // Best VMG angles.
    p.setBrush(col);
    p.setPen(Qt::NoPen);
    for (double a : {m_polar.bestUpwindTwa(w), m_polar.bestDownwindTwa(w)})
      p.drawEllipse(pos(a, m_polar.speedAt(w, a)), 3, 3);
    p.setPen(col);
    p.drawText(QPointF(width() - 46, 14 + 14 * k),
               QStringLiteral("%1 kn").arg(w));
  }
}

// --------------------------------------------------------------------------

PolarEditor::PolarEditor(const Polar& p, QWidget* parent)
    : QDialog(parent), m_name(p.name) {
  setWindowTitle(tr("Edit polar"));
  resize(720, 520);
  auto* l = new QVBoxLayout(this);
  l->addWidget(label(tr("Boat speed in knots. Rows: true wind angle, "
                        "columns: true wind speed. Empty cells count as "
                        "no data."),
                     "dim"));
  m_table = new QTableWidget;
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  l->addWidget(m_table, 1);
  auto* tools = new QHBoxLayout;
  auto* addRow = new QPushButton(tr("+ Angle"));
  auto* addCol = new QPushButton(tr("+ Wind speed"));
  auto* delRow = new QPushButton(tr("− Angle"));
  auto* delCol = new QPushButton(tr("− Wind speed"));
  for (auto* b : {addRow, addCol, delRow, delCol}) tools->addWidget(b);
  tools->addStretch(1);
  l->addLayout(tools);
  connect(addRow, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const double a = QInputDialog::getDouble(
        this, tr("Wind angle"), tr("True wind angle (°)"), 90, 0, 180, 0, &ok);
    if (!ok) return;
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setVerticalHeaderItem(r, new QTableWidgetItem(QString::number(a)));
  });
  connect(addCol, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const double w = QInputDialog::getDouble(
        this, tr("Wind speed"), tr("True wind speed (kn)"), 12, 1, 80, 0, &ok);
    if (!ok) return;
    const int c = m_table->columnCount();
    m_table->insertColumn(c);
    m_table->setHorizontalHeaderItem(c,
                                     new QTableWidgetItem(QString::number(w)));
  });
  connect(delRow, &QPushButton::clicked, this, [this] {
    if (m_table->currentRow() >= 0) m_table->removeRow(m_table->currentRow());
  });
  connect(delCol, &QPushButton::clicked, this, [this] {
    if (m_table->currentColumn() >= 0)
      m_table->removeColumn(m_table->currentColumn());
  });
  auto* box =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(box, &QDialogButtonBox::accepted, this, [this] {
    Polar p;
    QString err;
    if (!result(&p, &err)) {
      QMessageBox::warning(this, tr("Polar"), err);
      return;
    }
    accept();
  });
  connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  l->addWidget(box);
  fill(p);
}

void PolarEditor::fill(const Polar& p) {
  m_table->setRowCount(int(p.twa.size()));
  m_table->setColumnCount(int(p.tws.size()));
  for (int c = 0; c < p.tws.size(); ++c)
    m_table->setHorizontalHeaderItem(
        c, new QTableWidgetItem(QString::number(p.tws[c])));
  for (int r = 0; r < p.twa.size(); ++r) {
    m_table->setVerticalHeaderItem(
        r, new QTableWidgetItem(QString::number(p.twa[r])));
    for (int c = 0; c < p.tws.size(); ++c) {
      const double v = p.speed[r][c];
      m_table->setItem(
          r, c, new QTableWidgetItem(v > 0 ? QString::number(v, 'f', 2) : ""));
    }
  }
}

bool PolarEditor::result(Polar* out, QString* error) const {
  // Build the grid text and let the parser check it.
  QString text = "TWA\\TWS";
  for (int c = 0; c < m_table->columnCount(); ++c)
    text += '\t' + m_table->horizontalHeaderItem(c)->text();
  text += '\n';
  for (int r = 0; r < m_table->rowCount(); ++r) {
    text += m_table->verticalHeaderItem(r)->text();
    for (int c = 0; c < m_table->columnCount(); ++c) {
      const QTableWidgetItem* it = m_table->item(r, c);
      text += '\t' + (it ? it->text().trimmed().replace(',', '.') : QString());
    }
    text += '\n';
  }
  Polar p;
  p.name = m_name;
  if (!p.parse(text, error)) return false;
  if (out) *out = p;
  return true;
}

// --------------------------------------------------------------------------

PolarPanel::PolarPanel(QWidget* parent) : QWidget(parent) {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(6);
  m_name = label(QString(), "value");
  l->addWidget(m_name);
  m_info = label(QString(), "dim");
  m_info->setWordWrap(true);
  l->addWidget(m_info);
  m_diagram = new PolarDiagram;
  l->addWidget(m_diagram);
  auto* row = new QGridLayout;
  auto* imp = button(tr("Import"), "import");
  imp->setToolTip(
      tr("Polar file: .pol, .csv, .txt (OpenCPN, qtVlm, ORC "
         "tables, Expedition)"));
  auto* edit = button(tr("Edit"), "edit");
  auto* exp = button(tr("Export"), "export");
  auto* reset = button(tr("Generic"), "refresh");
  reset->setToolTip(tr("Back to the generic polar of a cruising yacht"));
  row->addWidget(imp, 0, 0);
  row->addWidget(edit, 0, 1);
  row->addWidget(exp, 1, 0);
  row->addWidget(reset, 1, 1);
  l->addLayout(row);

  connect(imp, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import polar"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        tr("Polar files (*.pol *.csv *.txt *.dat);;All files (*)"));
    if (path.isEmpty()) return;
    Polar p;
    QString err;
    if (!p.load(path, &err)) {
      QMessageBox::warning(this, tr("Import polar"), err);
      return;
    }
    PolarStore::instance().setPolar(p);
  });
  connect(edit, &QPushButton::clicked, this, [this] {
    PolarEditor dlg(PolarStore::instance().polar(), this);
    Polar p;
    if (dlg.exec() == QDialog::Accepted && dlg.result(&p, nullptr)) {
      if (!PolarStore::instance().isCustom()) p.name = tr("Own polar");
      PolarStore::instance().setPolar(p);
    }
  });
  connect(exp, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export polar"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/" +
            PolarStore::instance().polar().name + ".pol",
        tr("Polar files (*.pol)"));
    if (!path.isEmpty()) PolarStore::instance().polar().save(path);
  });
  connect(reset, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const double top = QInputDialog::getDouble(this, tr("Generic polar"),
                                               tr("Best speed under sail (kn)"),
                                               7.0, 2, 40, 1, &ok);
    if (ok) PolarStore::instance().reset(top);
  });
  connect(&PolarStore::instance(), &PolarStore::changed, this,
          &PolarPanel::sync);
  connect(&Theme::instance(), &Theme::changed, m_diagram,
          qOverload<>(&QWidget::update));
  sync();
}

void PolarPanel::sync() {
  const auto& store = PolarStore::instance();
  const Polar& p = store.polar();
  m_name->setText(p.name);
  m_info->setText(
      tr("%1 wind speeds (%2–%3 kn) · %4 angles · max. %5 kn · close "
         "hauled at 12 kn: %6°%7")
          .arg(p.tws.size())
          .arg(p.tws.first())
          .arg(p.tws.last())
          .arg(p.twa.size())
          .arg(p.maxSpeed(), 0, 'f', 1)
          .arg(int(std::round(p.bestUpwindTwa(12))))
          .arg(store.isCustom() ? QString() : tr(" · generic")));
  m_diagram->setPolar(p);
}
