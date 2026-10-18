#include "ui/PassagePage.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QCompleter>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "core/AppSettings.h"
#include "html_style.h"
#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "core/Passage.h"
#include "core/TrackRecorder.h"
#include "ui/ChartView.h"
#include "ui/DrawerPages.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

using seachart::Harbour;

namespace {

PassageService& svc() { return PassageService::instance(); }
QString U(const std::string& s) { return QString::fromStdString(s); }
std::string S(const QString& s) { return s.trimmed().toStdString(); }

QString timeText(time_t t) {
  if (t <= 0) return QStringLiteral("–");
  return QDateTime::fromSecsSinceEpoch(t).toString("ddd HH:mm");
}

QString compass(double deg) {
  static const char* const names[] = {"N",  "NNE", "NE", "ENE", "E",  "ESE",
                                      "SE", "SSE", "S",  "SSW", "SW", "WSW",
                                      "W",  "WNW", "NW", "NNW"};
  return names[int(std::floor(std::fmod(deg + 11.25 + 360, 360) / 22.5)) % 16];
}

QString kindLabel(const std::string& kind) {
  if (kind == "marina") return QObject::tr("Marina");
  if (kind == "harbour") return QObject::tr("Harbour");
  if (kind == "anchorage") return QObject::tr("Anchorage");
  if (kind == "buoy") return QObject::tr("Mooring buoy");
  return U(kind);
}

QString kindIcon(const std::string& kind) {
  return kind == "anchorage" || kind == "buoy" ? QStringLiteral("anchor")
                                               : QStringLiteral("harbour");
}

QString categoryLabel(const std::string& c) {
  if (c == "restaurant") return QObject::tr("Food & drink");
  if (c == "supermarket") return QObject::tr("Shopping");
  if (c == "fuel") return QObject::tr("Fuel");
  if (c == "chandlery") return QObject::tr("Chandlery");
  if (c == "service") return QObject::tr("Service");
  if (c == "transport") return QObject::tr("Transport");
  if (c == "sight") return QObject::tr("Sight");
  return U(c);
}

const char* const kKinds[] = {"marina", "harbour", "anchorage", "buoy"};

QLabel* label(const QString& text, const char* role = nullptr) {
  auto* l = new QLabel(text);
  if (role) l->setProperty("role", role);
  return l;
}

QPushButton* button(const QString& text, const QString& icon,
                    const char* role = nullptr) {
  auto* b = new QPushButton(text);
  b->setCursor(Qt::PointingHandCursor);
  if (role) b->setProperty("role", role);
  const bool light =
      role && (QByteArray(role) == "primary" || QByteArray(role) == "danger");
  if (!icon.isEmpty())
    b->setIcon(
        Icons::icon(icon, light ? QColor(Qt::white) : Theme::p().text, 18));
  b->setIconSize(QSize(18, 18));
  b->setProperty("iconName", icon);
  return b;
}

/** Icon color for a button in the current theme. */
QColor iconColor(const QPushButton* b) {
  const QByteArray role = b->property("role").toByteArray();
  if (role == "primary" || role == "danger") return Qt::white;
  if (role == "ghost") return Theme::p().textDim;
  return Theme::p().text;
}

/** Icons are pixmaps in the theme colors: repaint them after a change. */
void refreshIcons(QWidget* root) {
  for (auto* b : root->findChildren<QPushButton*>()) {
    const QString name = b->property("iconName").toString();
    if (!name.isEmpty()) b->setIcon(Icons::icon(name, iconColor(b), 18));
  }
}

/** Re-applies the stylesheet after a role change. */
void setRole(QPushButton* b, const char* role, const QString& icon) {
  b->setProperty("role", role);
  b->setProperty("iconName", icon);
  b->setIcon(Icons::icon(icon, iconColor(b), 18));
  b->style()->unpolish(b);
  b->style()->polish(b);
}

QWidget* textColumn(const QString& title, const QString& subtitle) {
  auto* w = new QWidget;
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(2);
  auto* t = label(title, "value");
  t->setTextFormat(Qt::PlainText);
  l->addWidget(t);
  if (!subtitle.isEmpty()) {
    auto* s = label(subtitle, "dim");
    s->setWordWrap(true);
    l->addWidget(s);
  }
  return w;
}

QWidget* settingRow(const QString& text, QWidget* control,
                    const QString& hint = {}) {
  auto* w = new QWidget;
  auto* l = new QHBoxLayout(w);
  l->setContentsMargins(4, 4, 4, 4);
  l->addWidget(textColumn(text, hint), 1);
  l->addWidget(control);
  return w;
}

QDoubleSpinBox* spin(double min, double max, double step, int decimals,
                     const QString& suffix) {
  auto* s = new QDoubleSpinBox;
  s->setRange(min, max);
  s->setSingleStep(step);
  s->setDecimals(decimals);
  s->setSuffix(suffix);
  s->setMinimumWidth(96);
  s->setAlignment(Qt::AlignRight);
  return s;
}

/** Scroll area whose content is laid out by the returned layout. */
QVBoxLayout* scrollPage(QWidget* page) {
  auto* outer = new QVBoxLayout(page);
  outer->setContentsMargins(0, 0, 0, 0);
  auto* scroll = new QScrollArea(page);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* content = new QWidget;
  content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* l = new QVBoxLayout(content);
  l->setContentsMargins(0, 0, 8, 0);
  l->setSpacing(6);
  scroll->setWidget(content);
  outer->addWidget(scroll);
  return l;
}

void clearLayout(QVBoxLayout* l) {
  while (QLayoutItem* it = l->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      w->hide();
      w->deleteLater();
    }
    delete it;
  }
}

/** Rounded card in the panel colors, for the result figures. */
class Card : public QWidget {
public:
  using QWidget::QWidget;

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::p().panelHover);
    p.drawRoundedRect(QRectF(rect()), 16, 16);
  }
};

/** Big figure with a small caption, like the cockpit cards. */
QWidget* figure(Figure*& value, const QString& caption) {
  value = new Figure(caption);
  return value;
}

QPixmap roundedThumb(const QString& path, int w, int h) {
  QImageReader r(path);
  r.setAutoTransform(true);
  QImage img = r.read();
  QPixmap out(w, h);
  out.fill(Qt::transparent);
  QPainter p(&out);
  p.setRenderHint(QPainter::Antialiasing);
  QPainterPath clip;
  clip.addRoundedRect(QRectF(0, 0, w, h), 12, 12);
  p.setClipPath(clip);
  if (img.isNull()) {
    p.fillRect(out.rect(), Theme::p().panelPressed);
  } else {
    const QImage s = img.scaled(w, h, Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    p.drawImage(QPoint((w - s.width()) / 2, (h - s.height()) / 2), s);
  }
  return out;
}

}  // namespace

// ------------------------------------------------------------------ Figure

Figure::Figure(const QString& caption, QWidget* parent)
    : QWidget(parent), m_caption(caption.toUpper()), m_value("–") {
  setMinimumHeight(58);
}

void Figure::setText(const QString& value) {
  m_value = value;
  update();
}

void Figure::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::TextAntialiasing);
  // number and unit: "10.7 NM" -> big "10.7", small "NM"
  const int sp = m_value.lastIndexOf(' ');
  const bool unit = sp > 0 && !m_value.at(sp + 1).isDigit();
  const QString num = unit ? m_value.left(sp) : m_value;
  const QFont big = Theme::numberFont(30, QFont::Light);
  p.setFont(big);
  p.setPen(pal.text);
  const QFontMetrics fm(big);
  p.drawText(QPointF(0, fm.ascent()), num);
  if (unit) {
    p.setFont(Theme::font(13, QFont::Medium));
    p.setPen(pal.textDim);
    p.drawText(QPointF(fm.horizontalAdvance(num) + 4, fm.ascent()),
               m_value.mid(sp + 1));
  }
  p.setFont(Theme::font(10, QFont::DemiBold));
  p.setPen(pal.textFaint);
  p.drawText(QPointF(0, height() - 4), m_caption);
}

// ------------------------------------------------------------ PassageLayer

PassageLayer::PassageLayer(QObject* parent) : MapLayer(parent) {
  connect(&svc(), &PassageService::harboursChanged, this, &MapLayer::changed);
  connect(&svc(), &PassageService::planChanged, this, &MapLayer::changed);
}

void PassageLayer::paint(QPainter& p, const ChartView& view) {
  const Palette& pal = Theme::p();
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF area =
      QRectF(0, 0, view.width(), view.height()).adjusted(-20, -20, 20, 20);

  // planned passage: dashed line with the arrival times at the turns
  const auto& plan = svc().currentPlan();
  const bool routed = NavStore::instance().route(svc().routeId()) != nullptr;
  if (plan.ok && !routed && plan.points.size() >= 2) {
    QPolygonF line;
    for (const auto& pt : plan.points)
      line << view.screenPos(pt.first, pt.second);
    QPen glow(pal.accentSoft, 9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(glow);
    p.drawPolyline(line);
    QPen pen(pal.accentBright, 3, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.drawPolyline(line);
    p.setFont(Theme::font(11, QFont::DemiBold));
    for (size_t i = 1; i + 1 < plan.points.size(); ++i) {
      const QPointF s = line[int(i)];
      p.setPen(Qt::NoPen);
      p.setBrush(pal.accentBright);
      p.drawEllipse(s, 4, 4);
      if (line.size() < 40) {
        const QString t = QDateTime::fromSecsSinceEpoch(plan.legs[i - 1].end)
                              .toString("HH:mm");
        p.setPen(pal.text);
        p.drawText(s + QPointF(7, -6), t);
      }
    }
  }

  // harbours and anchorages
  const auto& list = svc().harbours();
  int visible = 0;
  for (const auto& h : list)
    if (area.contains(view.screenPos(h.lat, h.lon))) ++visible;
  const bool labels = visible <= 40;
  p.setFont(Theme::font(11, QFont::Medium));
  for (const auto& h : list) {
    const QPointF s = view.screenPos(h.lat, h.lon);
    if (!area.contains(s)) continue;
    const bool endpoint =
        plan.ok && (h.id == svc().planFrom() || h.id == svc().planTo());
    const qreal r = endpoint ? 14 : 11;
    p.setPen(QPen(pal.panelBorder, 1));
    p.setBrush(endpoint ? pal.accent : pal.panel);
    p.drawEllipse(s, r, r);
    const QPixmap icon = Icons::pixmap(
        kindIcon(h.kind), endpoint ? QColor(Qt::white) : pal.accentBright,
        int(r * 1.2));
    p.drawPixmap(s - QPointF(icon.width(), icon.height()) /
                         (2 * icon.devicePixelRatio()),
                 icon);
    if (labels || endpoint) {
      const QString name = U(h.name);
      const QRectF tr(s.x() + r + 4, s.y() - 9, 220, 18);
      p.setPen(pal.water);
      p.drawText(tr.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, name);
      p.setPen(pal.text);
      p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, name);
    }
  }
}

// ---------------------------------------------------------------- PlanView

PlanView::PlanView(ChartView* chart, QWidget* parent)
    : QWidget(parent), m_chart(chart) {
  QVBoxLayout* l = scrollPage(this);

  l->addWidget(label(tr("ROUTE"), "section"));
  auto makeCombo = [] {
    auto* c = new QComboBox;
    c->setEditable(true);
    c->setInsertPolicy(QComboBox::NoInsert);
    c->completer()->setFilterMode(Qt::MatchContains);
    c->completer()->setCompletionMode(QCompleter::PopupCompletion);
    c->setMinimumWidth(200);
    return c;
  };
  m_from = makeCombo();
  m_to = makeCombo();
  auto* swap = new IconButton("route", tr("Swap departure and destination"));
  auto* grid = new QGridLayout;
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(6);
  grid->addWidget(label(tr("From"), "dim"), 0, 0);
  grid->addWidget(m_from, 0, 1);
  grid->addWidget(label(tr("To"), "dim"), 1, 0);
  grid->addWidget(m_to, 1, 1);
  grid->addWidget(swap, 0, 2, 2, 1, Qt::AlignVCenter);
  grid->setColumnStretch(1, 1);
  auto* gw = new QWidget;
  gw->setLayout(grid);
  l->addWidget(gw);
  connect(swap, &QAbstractButton::clicked, this, [this] {
    const qint64 a = selected(m_from), b = selected(m_to);
    setFrom(b);
    setTo(a);
  });

  l->addWidget(label(tr("DEPARTURE"), "section"));
  m_departure = new QDateTimeEdit;
  m_departure->setDisplayFormat("ddd dd.MM.yyyy  HH:mm");
  m_departure->setCalendarPopup(true);
  QDateTime next = QDateTime::currentDateTime();
  next = next.addSecs(3600 - next.time().minute() * 60 - next.time().second());
  m_departure->setDateTime(next);
  l->addWidget(m_departure);
  auto* quick = new QHBoxLayout;
  quick->setSpacing(4);
  auto addQuick = [&](const QString& text, auto when) {
    auto* b = new QPushButton(text);
    b->setProperty("role", "segment");
    b->setCursor(Qt::PointingHandCursor);
    connect(b, &QPushButton::clicked, this,
            [this, when] { m_departure->setDateTime(when()); });
    quick->addWidget(b, 1);
  };
  addQuick(tr("Now"), [] { return QDateTime::currentDateTime(); });
  addQuick(tr("+3 h"),
           [this] { return m_departure->dateTime().addSecs(10800); });
  addQuick(tr("Tomorrow 08:00"), [] {
    return QDateTime(QDate::currentDate().addDays(1), QTime(8, 0));
  });
  l->addLayout(quick);

  // boat settings, folded away by default
  auto* boatToggle = new QPushButton(tr("Boat settings"));
  boatToggle->setProperty("role", "ghost");
  boatToggle->setCheckable(true);
  boatToggle->setIcon(Icons::icon("sail", Theme::p().textDim, 18));
  boatToggle->setProperty("iconName", "sail");
  boatToggle->setCursor(Qt::PointingHandCursor);
  l->addWidget(boatToggle, 0, Qt::AlignLeft);
  m_boatBox = new QWidget;
  auto* bl = new QVBoxLayout(m_boatBox);
  bl->setContentsMargins(0, 0, 0, 0);
  bl->setSpacing(0);
  m_hull = spin(2, 30, 0.5, 1, " kn");
  m_designWind = spin(5, 40, 1, 0, " kn");
  m_noGo = spin(20, 70, 1, 0, "°");
  m_draft = spin(0.3, 6, 0.1, 1, " m");
  m_motor = new ToggleSwitch;
  m_motorSpeed = spin(1, 30, 0.5, 1, " kn");
  m_motorBelow = spin(0, 10, 0.5, 1, " kn");
  m_maxWind = spin(5, 60, 1, 0, " kn");
  m_maxGust = spin(5, 80, 1, 0, " kn");
  bl->addWidget(
      settingRow(tr("Hull speed"), m_hull, tr("Best speed under sail")));
  bl->addWidget(settingRow(tr("Wind for hull speed"), m_designWind));
  bl->addWidget(settingRow(tr("Closest angle to the wind"), m_noGo));
  bl->addWidget(
      settingRow(tr("Draft"), m_draft, tr("+0.5 m safety under the keel")));
  bl->addWidget(settingRow(tr("Engine"), m_motor,
                           tr("Motor when sailing is slower than…")));
  bl->addWidget(settingRow(tr("Engine below"), m_motorBelow));
  bl->addWidget(settingRow(tr("Engine speed"), m_motorSpeed));
  bl->addWidget(settingRow(tr("Warn above wind"), m_maxWind));
  bl->addWidget(settingRow(tr("Warn above gusts"), m_maxGust));
  bl->addWidget(label(tr("The sail plan of the Sails page is taken into "
                         "account (reefs, engine only)."),
                      "dim"));
  m_boatBox->setVisible(false);
  l->addWidget(m_boatBox);
  connect(boatToggle, &QPushButton::toggled, m_boatBox, &QWidget::setVisible);
  const seachart::BoatModel b = svc().boat();
  m_hull->setValue(b.hull_speed_kn);
  m_designWind->setValue(b.design_wind_kn);
  m_noGo->setValue(b.no_go_deg);
  m_draft->setValue(b.draft_m);
  m_motor->setChecked(b.motor);
  m_motorSpeed->setValue(b.motor_speed_kn);
  m_motorBelow->setValue(b.motor_below_kn);
  m_maxWind->setValue(b.max_wind_kn);
  m_maxGust->setValue(b.max_gust_kn);
  for (auto* s : {m_hull, m_designWind, m_noGo, m_draft, m_motorSpeed,
                  m_motorBelow, m_maxWind, m_maxGust})
    connect(s, &QDoubleSpinBox::valueChanged, this, &PlanView::storeBoat);
  connect(m_motor, &QAbstractButton::toggled, this, &PlanView::storeBoat);

  auto* actions = new QHBoxLayout;
  auto* calc = button(tr("Calculate"), "navigate", "primary");
  auto* compare = button(tr("Best departure"), "wind");
  compare->setToolTip(tr("Compares departures every 3 hours over two days"));
  actions->addWidget(calc, 1);
  actions->addWidget(compare, 1);
  l->addSpacing(6);
  l->addLayout(actions);
  connect(calc, &QPushButton::clicked, this, &PlanView::calculate);
  connect(compare, &QPushButton::clicked, this, &PlanView::compare);

  // result
  m_result = new Card;
  auto* rl = new QVBoxLayout(m_result);
  rl->setContentsMargins(16, 14, 16, 14);
  rl->setSpacing(8);
  auto* figs = new QHBoxLayout;
  figs->addWidget(figure(m_distance, tr("Distance")), 1);
  figs->addWidget(figure(m_duration, tr("Duration")), 1);
  figs->addWidget(figure(m_eta, tr("Arrival")), 1);
  rl->addLayout(figs);
  m_details = label(QString(), "dim");
  m_details->setWordWrap(true);
  rl->addWidget(m_details);
  m_weather = label(QString(), "dim");
  m_weather->setWordWrap(true);
  rl->addWidget(m_weather);
  l->addSpacing(6);
  l->addWidget(m_result);
  m_result->hide();

  m_legs = new QVBoxLayout;
  m_legs->setSpacing(6);
  l->addLayout(m_legs);

  auto* act2 = new QHBoxLayout;
  m_show = button(tr("Show"), "locate");
  m_route = button(tr("Create route"), "route");
  act2->addWidget(m_show, 1);
  act2->addWidget(m_route, 1);
  l->addLayout(act2);
  m_start = button(tr("Start passage"), "play", "primary");
  l->addWidget(m_start);
  l->addStretch(1);
  connect(m_show, &QPushButton::clicked, this, [this] {
    const auto& p = svc().currentPlan();
    if (!p.ok) return;
    double a = 90, b = 180, c = -90, d = -180;
    for (const auto& pt : p.points) {
      a = std::min(a, pt.first);
      b = std::min(b, pt.second);
      c = std::max(c, pt.first);
      d = std::max(d, pt.second);
    }
    m_chart->fitGeoBounds(a, b, c, d);
    emit m_chart->navigationRequested();
  });
  connect(m_route, &QPushButton::clicked, this, [this] {
    if (!svc().createRoute().isEmpty()) m_route->setText(tr("Route created"));
  });
  connect(m_start, &QPushButton::clicked, this, [this] {
    if (svc().underway())
      svc().stopPassage();
    else if (!svc().startPassage())
      QMessageBox::warning(this, tr("Passage"),
                           tr("The passage could not be started."));
  });

  connect(&svc(), &PassageService::harboursChanged, this,
          &PlanView::fillHarbours);
  connect(&svc(), &PassageService::passageChanged, this,
          &PlanView::updateButtons);
  connect(&svc(), &PassageService::planChanged, this, &PlanView::showPlan);
  connect(&Theme::instance(), &Theme::changed, this, &PlanView::showPlan);
  fillHarbours();
  showPlan();
}

void PlanView::storeBoat() {
  seachart::BoatModel b = svc().boat();
  b.hull_speed_kn = m_hull->value();
  b.design_wind_kn = m_designWind->value();
  b.no_go_deg = m_noGo->value();
  b.draft_m = m_draft->value();
  b.motor = m_motor->isChecked();
  b.motor_speed_kn = m_motorSpeed->value();
  b.motor_below_kn = m_motorBelow->value();
  b.max_wind_kn = m_maxWind->value();
  b.max_gust_kn = m_maxGust->value();
  svc().setBoat(b);
  m_motorSpeed->setEnabled(b.motor);
  m_motorBelow->setEnabled(b.motor);
}

qint64 PlanView::selected(QComboBox* c) const {
  const int i = c->findText(c->currentText());
  return i >= 0 ? c->itemData(i).toLongLong() : 0;
}

void PlanView::fillHarbours() {
  const qint64 from = selected(m_from), to = selected(m_to);
  for (QComboBox* c : {m_from, m_to}) {
    QSignalBlocker block(c);
    c->clear();
    for (const auto& h : svc().harbours())
      c->addItem(Icons::icon(kindIcon(h.kind), Theme::p().accentBright, 18),
                 U(h.name), QVariant::fromValue<qint64>(h.id));
    c->setCurrentIndex(-1);
    c->setCurrentText(QString());
  }
  m_from->lineEdit()->setPlaceholderText(tr("Harbour or anchorage"));
  m_to->lineEdit()->setPlaceholderText(tr("Harbour or anchorage"));
  QSettings s;
  setFrom(from               ? from
          : svc().planFrom() ? svc().planFrom()
                             : s.value("passage/from").toLongLong());
  setTo(to               ? to
        : svc().planTo() ? svc().planTo()
                         : s.value("passage/to").toLongLong());
}

void PlanView::setFrom(qint64 id) {
  const int i = m_from->findData(QVariant::fromValue<qint64>(id));
  if (i >= 0) m_from->setCurrentIndex(i);
}

void PlanView::setTo(qint64 id) {
  const int i = m_to->findData(QVariant::fromValue<qint64>(id));
  if (i >= 0) m_to->setCurrentIndex(i);
}

void PlanView::calculate() {
  QSettings s;
  s.setValue("passage/from", selected(m_from));
  s.setValue("passage/to", selected(m_to));
  QApplication::setOverrideCursor(Qt::WaitCursor);
  svc().plan(selected(m_from), selected(m_to), m_departure->dateTime());
  QApplication::restoreOverrideCursor();
}

void PlanView::compare() {
  const qint64 from = selected(m_from), to = selected(m_to);
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QDateTime best;
  double bestHours = 1e9;
  QString table;
  QTextStream o(&table);
  const QDateTime base = m_departure->dateTime();
  for (int k = 0; k <= 16; ++k) {
    const QDateTime dep = base.addSecs(k * 3 * 3600);
    const seachart::PassagePlan p = svc().plan(from, to, dep);
    if (!p.ok) break;
    o << dep.toString("ddd HH:mm") << "   "
      << PassageService::durationText(p.Hours()) << "   "
      << tr("gusts %1 kn").arg(p.max_gust_kn, 0, 'f', 0)
      << (p.warnings ? "  ⚠" : "") << "\n";
    if (!p.warnings && p.Hours() < bestHours) {
      bestHours = p.Hours();
      best = dep;
    }
  }
  if (best.isValid()) {
    m_departure->setDateTime(best);
    svc().plan(from, to, best);
  }
  QApplication::restoreOverrideCursor();
  QMessageBox box(this);
  box.setWindowTitle(tr("Best departure"));
  box.setText(best.isValid()
                  ? tr("Fastest departure within your limits: %1 (%2)")
                        .arg(best.toString("ddd dd.MM. HH:mm"),
                             PassageService::durationText(bestHours))
                  : tr("No departure within your wind limits."));
  box.setDetailedText(table);
  box.exec();
}

void PlanView::showPlan() {
  clearLayout(m_legs);
  const auto& p = svc().currentPlan();
  const Palette& pal = Theme::p();
  m_result->setVisible(p.ok || !p.error.empty());
  if (!p.ok) {
    m_distance->setText(QStringLiteral("–"));
    m_duration->setText(QStringLiteral("–"));
    m_eta->setText(QStringLiteral("–"));
    QString err = U(p.error);
    if (err.contains("no connection"))
      err =
          tr("No connection by water was found. The navigation grid may "
             "be too coarse for a narrow passage: rebuild it with "
             "'seachart navgrid --nav-resolution 0.002'.");
    else if (err.contains("no navigable water"))
      err =
          tr("No navigable water near the harbour. Is the area covered "
             "by 'seachart navgrid'?");
    m_details->setText(err);
    m_weather->clear();
    updateButtons();
    return;
  }
  m_distance->setText(AppSettings::instance().distanceText(p.distance_nm));
  m_duration->setText(PassageService::durationText(p.Hours()));
  m_eta->setText(QDateTime::fromSecsSinceEpoch(p.arrival).toString("HH:mm"));
  QStringList d;
  if (p.motor_nm > 0.05)
    d << tr("Engine %1").arg(AppSettings::instance().distanceText(p.motor_nm));
  if (p.max_wind_kn > 0)
    d << tr("wind up to %1 kn, gusts %2 kn")
             .arg(p.max_wind_kn, 0, 'f', 0)
             .arg(p.max_gust_kn, 0, 'f', 0);
  if (p.max_wave_m >= 0) d << tr("waves %1 m").arg(p.max_wave_m, 0, 'f', 1);
  QString details = d.join(" · ");
  if (p.warnings)
    details += "\n" + tr("⚠ %n leg(s) above your wind or gust limit", nullptr,
                         p.warnings);
  if (!p.grid_checked)
    details += "\n" + tr("⚠ No navigation grid: straight line, NOT checked "
                         "for land or shallows.");
  if (p.beyond_forecast && p.used_weather)
    details += "\n" + tr("The passage ends after the end of the forecast.");
  m_details->setText(details);
  m_weather->setText(svc().weatherSource());

  int i = 0;
  for (const auto& leg : p.legs) {
    auto* row = new ListRow;
    if (leg.warning) row->setHighlight(pal.warning);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(14, 8, 12, 8);
    auto* icon = new QLabel;
    const bool motor = leg.motor_nm > leg.distance_nm / 2;
    icon->setPixmap(Icons::pixmap(motor ? "engine" : "sail",
                                  leg.warning ? pal.warning : pal.accentBright,
                                  22));
    rl->addWidget(icon);
    QString title =
        QStringLiteral("%1  ·  %2°  ·  %3")
            .arg(timeText(leg.start))
            .arg(int(std::lround(leg.course_deg)) % 360, 3, 10, QChar('0'))
            .arg(AppSettings::instance().distanceText(leg.distance_nm));
    QString sub = AppSettings::instance().speedText(leg.speed_kn);
    if (leg.avg_wind_kn > 0)
      sub += tr(" · wind %1 kn %2, TWA %3°, gusts %4")
                 .arg(leg.avg_wind_kn, 0, 'f', 0)
                 .arg(compass(leg.wind_dir))
                 .arg(leg.twa_deg, 0, 'f', 0)
                 .arg(leg.max_gust_kn, 0, 'f', 0);
    if (leg.max_wave_m >= 0)
      sub += tr(" · %1 m").arg(leg.max_wave_m, 0, 'f', 1);
    rl->addWidget(textColumn(title, sub), 1);
    const double lat = leg.lat1, lon = leg.lon1;
    connect(row, &ListRow::clicked, this, [this, lat, lon] {
      m_chart->centerOn(lat, lon);
      emit m_chart->navigationRequested();
    });
    m_legs->addWidget(row);
    ++i;
  }
  m_route->setText(tr("Create route"));
  updateButtons();
}

void PlanView::updateButtons() {
  const bool ok = svc().currentPlan().ok;
  const bool underway = svc().underway();
  m_show->setEnabled(ok);
  m_route->setEnabled(ok);
  m_start->setEnabled(ok || underway);
  m_start->setText(underway ? tr("End passage") : tr("Start passage"));
  setRole(m_start, underway ? "danger" : "primary", underway ? "stop" : "play");
}

// ------------------------------------------------------------- HarbourView

HarbourView::HarbourView(ChartView* chart, QWidget* parent)
    : QWidget(parent), m_chart(chart) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  m_stack = new QStackedWidget;
  outer->addWidget(m_stack);

  // list
  auto* listPage = new QWidget;
  QVBoxLayout* ll = scrollPage(listPage);
  auto* top = new QHBoxLayout;
  m_search = new QLineEdit;
  m_search->setPlaceholderText(tr("Search harbours and anchorages"));
  m_search->setClearButtonEnabled(true);
  m_search->addAction(Icons::icon("search", Theme::p().textDim, 18),
                      QLineEdit::LeadingPosition);
  auto* add = new IconButton("plus", tr("New harbour or anchorage"));
  top->addWidget(m_search, 1);
  top->addWidget(add);
  ll->addLayout(top);
  m_rows = new QVBoxLayout;
  m_rows->setSpacing(8);
  ll->addLayout(m_rows);
  m_empty = label(tr("No harbours yet. They are created from the "
                     "OpenSeaMap data by 'seachart download', or add your "
                     "own with + or from the chart's context menu."),
                  "dim");
  m_empty->setWordWrap(true);
  m_empty->setAlignment(Qt::AlignCenter);
  m_empty->setContentsMargins(20, 30, 20, 30);
  ll->addWidget(m_empty);
  ll->addStretch(1);
  // which seachart database is used (shared with tools/seachart)
  auto* dbRow = new QHBoxLayout;
  auto* dbLabel = label(QString(), "dim");
  dbLabel->setWordWrap(true);
  auto* dbButton = new QPushButton(tr("Change"));
  dbButton->setProperty("role", "ghost");
  dbButton->setCursor(Qt::PointingHandCursor);
  dbRow->addWidget(dbLabel, 1);
  dbRow->addWidget(dbButton);
  ll->addLayout(dbRow);
  auto updateDb = [dbLabel] {
    dbLabel->setText(tr("Database: %1").arg(svc().databasePath()));
  };
  updateDb();
  connect(&svc(), &PassageService::databaseChanged, dbLabel, updateDb);
  connect(dbButton, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("seachart database"),
        QFileInfo(svc().databasePath()).absolutePath(),
        tr("seachart database (*.db)"));
    if (path.isEmpty()) return;
    QString err;
    if (!svc().openDatabase(path, &err))
      QMessageBox::warning(this, tr("seachart database"), err);
  });
  m_stack->addWidget(listPage);
  connect(m_search, &QLineEdit::textChanged, this, &HarbourView::rebuildList);
  connect(add, &QAbstractButton::clicked, this, [this] {
    double lat, lon;
    m_chart->center(lat, lon);
    newAt(lat, lon);
  });

  // editor
  auto* detail = new QWidget;
  QVBoxLayout* dl = scrollPage(detail);
  auto* back = new QPushButton(tr("‹ All harbours"));
  back->setProperty("role", "ghost");
  back->setCursor(Qt::PointingHandCursor);
  dl->addWidget(back, 0, Qt::AlignLeft);
  connect(back, &QPushButton::clicked, this, [this] {
    m_stack->setCurrentIndex(0);
    rebuildList();
  });
  m_title = label(QString(), "title");
  m_title->setWordWrap(true);
  dl->addWidget(m_title);

  m_photos = new QVBoxLayout;
  dl->addLayout(m_photos);

  dl->addWidget(label(tr("DETAILS"), "section"));
  m_kind = new QComboBox;
  for (auto k : kKinds)
    m_kind->addItem(Icons::icon(kindIcon(k), Theme::p().accentBright, 18),
                    kindLabel(k));
  auto line = [](const QString& placeholder = {}) {
    auto* e = new QLineEdit;
    e->setPlaceholderText(placeholder);
    e->setMinimumWidth(170);
    return e;
  };
  m_name = line();
  m_lat = line("54.3812");
  m_lon = line("10.1634");
  m_depth = line(tr("m"));
  m_berths = line();
  m_vhf = line();
  m_phone = line();
  m_website = line("https://");
  m_facilities = line(tr("fuel, water, showers…"));
  dl->addWidget(settingRow(tr("Type"), m_kind));
  dl->addWidget(settingRow(tr("Name"), m_name));
  dl->addWidget(settingRow(tr("Latitude"), m_lat, tr("decimal degrees")));
  dl->addWidget(settingRow(tr("Longitude"), m_lon));
  dl->addWidget(settingRow(tr("Water depth"), m_depth));
  dl->addWidget(settingRow(tr("Berths"), m_berths));
  dl->addWidget(settingRow(tr("VHF channel"), m_vhf));
  dl->addWidget(settingRow(tr("Phone"), m_phone));
  dl->addWidget(settingRow(tr("Website"), m_website));
  dl->addWidget(settingRow(tr("Facilities"), m_facilities));
  auto* stars = new QWidget;
  auto* sl = new QHBoxLayout(stars);
  sl->setContentsMargins(0, 0, 0, 0);
  sl->setSpacing(4);
  m_rating = new QButtonGroup(this);
  for (int i = 0; i <= 5; ++i) {
    auto* b = new QPushButton(i == 0 ? QStringLiteral("–")
                                     : QString(i, QChar(0x2605)));
    b->setCheckable(true);
    b->setProperty("role", "segment");
    b->setCursor(Qt::PointingHandCursor);
    m_rating->addButton(b, i);
    sl->addWidget(b);
  }
  dl->addWidget(label(tr("Rating"), "dim"));
  dl->addWidget(stars);
  dl->addWidget(label(tr("NOTES"), "section"));
  m_notes = new QPlainTextEdit;
  m_notes->setPlaceholderText(
      tr("Approach, holding ground, protection, harbour master, …"));
  m_notes->setMinimumHeight(110);
  dl->addWidget(m_notes);

  auto* act = new QHBoxLayout;
  auto* saveB = button(tr("Save"), "check", "primary");
  auto* showB = button(tr("Show"), "locate");
  m_delete = button(QString(), "trash", "danger");
  m_delete->setToolTip(tr("Delete"));
  act->addWidget(saveB, 2);
  act->addWidget(showB, 1);
  act->addWidget(m_delete);
  dl->addLayout(act);
  auto* plan = new QHBoxLayout;
  auto* fromB = button(tr("Depart from here"), "navigate");
  auto* toB = button(tr("Sail here"), "flag");
  plan->addWidget(fromB, 1);
  plan->addWidget(toB, 1);
  dl->addLayout(plan);

  dl->addWidget(label(tr("WEATHER"), "section"));
  m_forecast = new QVBoxLayout;
  m_forecast->setSpacing(2);
  dl->addLayout(m_forecast);
  dl->addWidget(label(tr("NEARBY"), "section"));
  m_nearby = new QVBoxLayout;
  m_nearby->setSpacing(6);
  dl->addLayout(m_nearby);
  dl->addStretch(1);
  m_stack->addWidget(detail);

  connect(saveB, &QPushButton::clicked, this, &HarbourView::save);
  connect(showB, &QPushButton::clicked, this, [this] {
    bool a, b;
    const double lat = m_lat->text().replace(',', '.').toDouble(&a);
    const double lon = m_lon->text().replace(',', '.').toDouble(&b);
    if (a && b) {
      m_chart->centerOn(lat, lon);
      emit m_chart->navigationRequested();
    }
  });
  connect(m_delete, &QPushButton::clicked, this, [this] {
    if (!m_id) return;
    if (QMessageBox::question(
            this, tr("Delete"),
            tr("Delete '%1' and its photos?").arg(m_name->text())) !=
        QMessageBox::Yes)
      return;
    svc().removeHarbour(m_id);
    m_stack->setCurrentIndex(0);
  });
  connect(fromB, &QPushButton::clicked, this, [this] {
    if (!m_id) save();
    if (m_id) emit planFrom(m_id);
  });
  connect(toB, &QPushButton::clicked, this, [this] {
    if (!m_id) save();
    if (m_id) emit planTo(m_id);
  });
  connect(&svc(), &PassageService::harboursChanged, this, [this] {
    if (m_stack->currentIndex() == 0) rebuildList();
  });
  connect(&Theme::instance(), &Theme::changed, this, &HarbourView::rebuildList);
  rebuildList();
}

void HarbourView::rebuildList() {
  clearLayout(m_rows);
  const Palette& pal = Theme::p();
  const QString f = m_search->text().trimmed();
  const auto& own = NavStore::instance().ownShip();
  QVector<const Harbour*> list;
  for (const auto& h : svc().harbours()) {
    if (!f.isEmpty() && !U(h.name).contains(f, Qt::CaseInsensitive) &&
        !U(h.notes).contains(f, Qt::CaseInsensitive) &&
        !kindLabel(h.kind).contains(f, Qt::CaseInsensitive))
      continue;
    list.push_back(&h);
  }
  if (own.valid)  // nearest first when the position is known
    std::sort(list.begin(), list.end(),
              [&](const Harbour* a, const Harbour* b) {
                return geo::distanceNm(own.lat, own.lon, a->lat, a->lon) <
                       geo::distanceNm(own.lat, own.lon, b->lat, b->lon);
              });
  int shown = 0;
  for (const Harbour* h : list) {
    if (++shown > 200) break;  // the search narrows larger lists
    auto* row = new ListRow;
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 8, 10, 8);
    auto* icon = new QLabel;
    icon->setPixmap(Icons::pixmap(kindIcon(h->kind), pal.accentBright, 22));
    l->addWidget(icon);
    QStringList sub{kindLabel(h->kind)};
    if (own.valid)
      sub << AppSettings::instance().distanceText(
          geo::distanceNm(own.lat, own.lon, h->lat, h->lon));
    if (!h->notes.empty()) sub << U(h->notes).section('\n', 0, 0).left(60);
    l->addWidget(
        textColumn(U(h->name) + (h->rating > 0
                                     ? "  " + QString(h->rating, QChar(0x2605))
                                     : QString()),
                   sub.join(" · ")),
        1);
    const qint64 id = h->id;
    auto* go = new IconButton("navigate", tr("Plan passage to here"));
    connect(go, &QAbstractButton::clicked, this,
            [this, id] { emit planTo(id); });
    l->addWidget(go);
    connect(row, &ListRow::clicked, this, [this, id] { edit(id); });
    m_rows->addWidget(row);
  }
  m_empty->setVisible(list.isEmpty());
}

void HarbourView::edit(qint64 id) {
  if (const Harbour* h = svc().harbour(id)) showDetail(*h);
}

void HarbourView::newAt(double lat, double lon) {
  Harbour h;
  h.kind = "anchorage";
  h.lat = lat;
  h.lon = lon;
  showDetail(h);
  m_name->setFocus();
}

void HarbourView::showDetail(const Harbour& h) {
  m_id = h.id;
  m_title->setText(h.id ? U(h.name) : tr("New harbour or anchorage"));
  int k = 0;
  for (int i = 0; i < 4; ++i)
    if (h.kind == kKinds[i]) k = i;
  m_kind->setCurrentIndex(k);
  m_name->setText(U(h.name));
  m_lat->setText(QString::number(h.lat, 'f', 5));
  m_lon->setText(QString::number(h.lon, 'f', 5));
  m_depth->setText(h.depth >= 0 ? QString::number(h.depth, 'f', 1) : QString());
  m_berths->setText(h.berths >= 0 ? QString::number(h.berths) : QString());
  m_vhf->setText(U(h.vhf));
  m_phone->setText(U(h.phone));
  m_website->setText(U(h.website));
  m_facilities->setText(U(h.facilities));
  if (auto* b = m_rating->button(std::clamp(h.rating, 0, 5)))
    b->setChecked(true);
  m_notes->setPlainText(U(h.notes));
  m_delete->setEnabled(h.id != 0);
  fillPhotos();

  // forecast for the next two days, every 6 hours
  clearLayout(m_forecast);
  const Palette& pal = Theme::p();
  const auto& w = svc().weatherFor(QDateTime::currentDateTime());
  const time_t now = QDateTime::currentSecsSinceEpoch() / 3600 * 3600;
  int n = 0;
  for (time_t t = now; n < 9; t += 6 * 3600, ++n) {
    const seachart::WeatherSample s = w.At(h.lat, h.lon, t);
    if (!s.valid) break;
    auto* row = new QWidget;
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(4, 2, 4, 2);
    auto* when = label(timeText(t), "dim");
    when->setMinimumWidth(80);
    rl->addWidget(when);
    auto* arrow = new QLabel;
    QPixmap pm = Icons::pixmap(
        "arrow-up", s.gust_kn >= 25 ? pal.warning : pal.accentBright, 18);
    // the arrow shows where the wind blows to
    QTransform rot;
    rot.rotate(s.wind_dir + 180);
    arrow->setPixmap(pm.transformed(rot, Qt::SmoothTransformation));
    rl->addWidget(arrow);
    rl->addWidget(
        label(tr("%1 kn %2").arg(s.wind_kn, 0, 'f', 0).arg(compass(s.wind_dir)),
              "value"),
        1);
    rl->addWidget(label(tr("gusts %1").arg(s.gust_kn, 0, 'f', 0), "dim"));
    if (s.wave_m >= 0)
      rl->addWidget(label(tr("%1 m").arg(s.wave_m, 0, 'f', 1), "dim"));
    m_forecast->addWidget(row);
  }
  if (!n) {
    auto* none = label(tr("No forecast offline for this place."), "dim");
    none->setWordWrap(true);
    m_forecast->addWidget(none);
  }

  // shops, restaurants, … from OpenStreetMap
  clearLayout(m_nearby);
  int count = 0;
  if (h.id)
    for (const auto& p : svc().db().PoisNear(h.lat, h.lon, 1500)) {
      if (++count > 40) break;
      QStringList sub{categoryLabel(p.category),
                      QStringLiteral("%1 m").arg(int(p.distance_m))};
      if (!p.opening_hours.empty()) sub << U(p.opening_hours);
      auto* row = new ListRow;
      row->setMinimumHeight(48);
      auto* rl = new QHBoxLayout(row);
      rl->setContentsMargins(14, 6, 10, 6);
      rl->addWidget(
          textColumn(p.name.empty() ? categoryLabel(p.category) : U(p.name),
                     sub.join(" · ")),
          1);
      const double lat = p.lat, lon = p.lon;
      connect(row, &ListRow::clicked, this, [this, lat, lon] {
        m_chart->centerOn(lat, lon);
        emit m_chart->navigationRequested();
      });
      m_nearby->addWidget(row);
    }
  if (!count) {
    auto* none = label(tr("No places stored. 'seachart pois' downloads "
                          "restaurants, shops and more from OpenStreetMap."),
                       "dim");
    none->setWordWrap(true);
    m_nearby->addWidget(none);
  }
  m_stack->setCurrentIndex(1);
}

void HarbourView::fillPhotos() {
  clearLayout(m_photos);
  auto* strip = new QWidget;
  auto* sl = new QHBoxLayout(strip);
  sl->setContentsMargins(0, 4, 0, 4);
  sl->setSpacing(8);
  if (m_id) {
    for (const auto& ph : svc().db().Photos(m_id)) {
      const QString path = U(ph.path);
      auto* thumb = new QPushButton;
      thumb->setFlat(true);
      thumb->setStyleSheet("background: transparent; padding: 0;");
      thumb->setCursor(Qt::PointingHandCursor);
      thumb->setIcon(roundedThumb(path, 150, 100));
      thumb->setIconSize(QSize(150, 100));
      thumb->setToolTip(U(ph.caption));
      connect(thumb, &QPushButton::clicked, this,
              [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
      const qint64 pid = ph.id;
      thumb->setContextMenuPolicy(Qt::CustomContextMenu);
      connect(thumb, &QWidget::customContextMenuRequested, this, [this, pid] {
        if (QMessageBox::question(this, tr("Photos"),
                                  tr("Remove this photo?")) ==
            QMessageBox::Yes) {
          svc().removePhoto(pid);
          fillPhotos();
        }
      });
      sl->addWidget(thumb);
    }
  }
  auto* add = new QPushButton;
  add->setFixedSize(100, 100);
  add->setIcon(Icons::icon("camera", Theme::p().textDim, 28));
  add->setIconSize(QSize(28, 28));
  add->setIconSize(QSize(28, 28));
  add->setToolTip(tr("Add photos"));
  add->setCursor(Qt::PointingHandCursor);
  connect(add, &QPushButton::clicked, this, [this] {
    if (!m_id) save();
    if (!m_id) return;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add photos"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        tr("Images (*.jpg *.jpeg *.png *.JPG *.JPEG *.PNG)"));
    if (files.isEmpty()) return;
    bool ok = false;
    const QString caption =
        QInputDialog::getText(this, tr("Add photos"), tr("Caption (optional)"),
                              QLineEdit::Normal, QString(), &ok);
    for (const QString& f : files) {
      QString err;
      if (!svc().addPhoto(m_id, f, ok ? caption : QString(), &err))
        QMessageBox::warning(this, tr("Add photos"), err);
    }
    fillPhotos();
  });
  sl->addWidget(add);
  sl->addStretch(1);
  auto* scroll = new QScrollArea;
  scroll->setWidget(strip);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setFixedHeight(116);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_photos->addWidget(scroll);
}

void HarbourView::save() {
  bool a, b;
  const double lat = m_lat->text().replace(',', '.').toDouble(&a);
  const double lon = m_lon->text().replace(',', '.').toDouble(&b);
  if (m_name->text().trimmed().isEmpty() || !a || !b || std::fabs(lat) > 90 ||
      std::fabs(lon) > 180) {
    QMessageBox::warning(this, tr("Harbour"),
                         tr("Please enter a name and the position in decimal "
                            "degrees, e.g. 54.3812 and 10.1634."));
    return;
  }
  Harbour h;
  if (const Harbour* old = svc().harbour(m_id)) h = *old;
  h.id = m_id;
  h.kind = kKinds[std::max(0, m_kind->currentIndex())];
  h.name = S(m_name->text());
  h.lat = lat;
  h.lon = lon;
  bool ok;
  const double depth = m_depth->text().replace(',', '.').toDouble(&ok);
  h.depth = ok ? depth : -1;
  const int berths = m_berths->text().toInt(&ok);
  h.berths = ok ? berths : -1;
  h.vhf = S(m_vhf->text());
  h.phone = S(m_phone->text());
  h.website = S(m_website->text());
  h.facilities = S(m_facilities->text());
  h.rating = std::max(0, m_rating->checkedId());
  h.notes = m_notes->toPlainText().toStdString();
  if (!svc().saveHarbour(h)) {
    QMessageBox::warning(
        this, tr("Harbour"),
        tr("Saving failed: %1").arg(U(svc().db().LastError())));
    return;
  }
  m_id = h.id;
  m_title->setText(U(h.name));
  m_delete->setEnabled(true);
}

// ------------------------------------------------------------- LogbookView

LogbookView::LogbookView(ChartView* chart, QWidget* parent)
    : QWidget(parent), m_chart(chart) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  m_stack = new QStackedWidget;
  outer->addWidget(m_stack);

  auto* listPage = new QWidget;
  QVBoxLayout* ll = scrollPage(listPage);
  auto* exp = button(tr("Export logbook"), "export");
  ll->addWidget(exp);
  m_rows = new QVBoxLayout;
  m_rows->setSpacing(8);
  ll->addLayout(m_rows);
  m_empty = label(tr("No passages yet. Plan a passage and press \"Start "
                     "passage\": the route, the track and regular log "
                     "entries are recorded here."),
                  "dim");
  m_empty->setWordWrap(true);
  m_empty->setAlignment(Qt::AlignCenter);
  m_empty->setContentsMargins(20, 30, 20, 30);
  ll->addWidget(m_empty);
  ll->addStretch(1);
  m_stack->addWidget(listPage);
  connect(exp, &QPushButton::clicked, this, &LogbookView::exportAll);

  auto* detail = new QWidget;
  QVBoxLayout* dl = scrollPage(detail);
  auto* back = new QPushButton(tr("‹ Logbook"));
  back->setProperty("role", "ghost");
  back->setCursor(Qt::PointingHandCursor);
  dl->addWidget(back, 0, Qt::AlignLeft);
  connect(back, &QPushButton::clicked, this, [this] {
    m_stack->setCurrentIndex(0);
    rebuild();
  });
  m_title = label(QString(), "title");
  m_title->setWordWrap(true);
  dl->addWidget(m_title);
  auto* card = new Card;
  auto* cl = new QVBoxLayout(card);
  cl->setContentsMargins(16, 14, 16, 14);
  auto* figs = new QHBoxLayout;
  figs->addWidget(figure(m_distance, tr("Distance")), 1);
  figs->addWidget(figure(m_duration, tr("Duration")), 1);
  figs->addWidget(figure(m_maxSog, tr("Max. SOG")), 1);
  cl->addLayout(figs);
  m_summary = label(QString(), "dim");
  m_summary->setWordWrap(true);
  cl->addWidget(m_summary);
  dl->addWidget(card);
  dl->addWidget(label(tr("NOTES"), "section"));
  m_notes = new QPlainTextEdit;
  m_notes->setPlaceholderText(tr("Crew, events, remarks…"));
  m_notes->setMinimumHeight(90);
  dl->addWidget(m_notes);
  auto* act = new QHBoxLayout;
  auto* saveB = button(tr("Save notes"), "check", "primary");
  auto* trackB = button(tr("Track"), "flag");
  auto* delB = button(QString(), "trash", "danger");
  delB->setToolTip(tr("Delete"));
  act->addWidget(saveB, 2);
  act->addWidget(trackB, 1);
  act->addWidget(delB);
  dl->addLayout(act);
  dl->addWidget(label(tr("LOG"), "section"));
  m_entries = new QVBoxLayout;
  m_entries->setSpacing(6);
  dl->addLayout(m_entries);
  dl->addStretch(1);
  m_stack->addWidget(detail);

  connect(saveB, &QPushButton::clicked, this, [this] {
    seachart::Trip t;
    if (!svc().logbook().GetTrip(m_trip, t)) return;
    t.notes = m_notes->toPlainText().toStdString();
    svc().logbook().UpdateTrip(t);
  });
  connect(trackB, &QPushButton::clicked, this, [this] {
    seachart::Trip t;
    if (!svc().logbook().GetTrip(m_trip, t)) return;
    const Route* track = TrackRecorder::instance().track(U(t.track_guid));
    if (!track && TrackRecorder::instance().activeTrack().id == U(t.track_guid))
      track = &TrackRecorder::instance().activeTrack();
    if (!track || track->points.isEmpty()) {
      QMessageBox::information(this, tr("Logbook"),
                               tr("The track of this passage was not found."));
      return;
    }
    TrackRecorder::instance().setTrackVisible(track->id, true);
    double a = 90, b = 180, c = -90, d = -180;
    for (const auto& p : track->points) {
      a = std::min(a, p.lat);
      b = std::min(b, p.lon);
      c = std::max(c, p.lat);
      d = std::max(d, p.lon);
    }
    m_chart->fitGeoBounds(a, b, c, d);
    emit m_chart->navigationRequested();
  });
  connect(delB, &QPushButton::clicked, this, [this] {
    if (svc().tripId() == m_trip) return;  // under way
    if (QMessageBox::question(this, tr("Logbook"),
                              tr("Delete this passage from the logbook?")) !=
        QMessageBox::Yes)
      return;
    svc().logbook().DeleteTrip(m_trip);
    m_stack->setCurrentIndex(0);
    rebuild();
  });
  connect(&svc(), &PassageService::passageChanged, this, &LogbookView::rebuild);
  connect(&svc(), &PassageService::databaseChanged, this,
          &LogbookView::rebuild);
  connect(&Theme::instance(), &Theme::changed, this, &LogbookView::rebuild);
  rebuild();
}

void LogbookView::rebuild() {
  clearLayout(m_rows);
  const Palette& pal = Theme::p();
  const auto trips = svc().logbook().Trips();
  for (const auto& t : trips) {
    auto* row = new ListRow;
    if (!t.ended) row->setHighlight(pal.success);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 8, 10, 8);
    auto* icon = new QLabel;
    icon->setPixmap(
        Icons::pixmap("book", t.ended ? pal.accentBright : pal.success, 22));
    l->addWidget(icon);
    QStringList sub{
        QDateTime::fromSecsSinceEpoch(t.started).toString("dd.MM.yyyy HH:mm"),
        AppSettings::instance().distanceText(t.distance_nm),
        PassageService::durationText(t.Hours())};
    if (!t.ended) sub << tr("under way");
    l->addWidget(textColumn(U(t.name), sub.join(" · ")), 1);
    const qint64 id = t.id;
    connect(row, &ListRow::clicked, this, [this, id] { showTrip(id); });
    m_rows->addWidget(row);
  }
  m_empty->setVisible(trips.empty());
  if (m_stack->currentIndex() == 1 && m_trip) showTrip(m_trip);
}

void LogbookView::showTrip(qint64 id) {
  seachart::Trip t;
  if (!svc().logbook().GetTrip(id, t)) return;
  const auto& units = AppSettings::instance();
  m_trip = id;
  m_title->setText(U(t.name));
  if (m_notes->property("trip").toLongLong() != id) {
    m_notes->setPlainText(U(t.notes));
    m_notes->setProperty("trip", QVariant::fromValue<qint64>(id));
  }
  m_distance->setText(units.distanceText(t.distance_nm));
  m_duration->setText(PassageService::durationText(t.Hours()));
  m_maxSog->setText(units.speedText(t.max_sog));
  QString sum = tr("%1 – %2").arg(
      QDateTime::fromSecsSinceEpoch(t.started).toString("ddd dd.MM. HH:mm"),
      t.ended ? QDateTime::fromSecsSinceEpoch(t.ended).toString("HH:mm")
              : tr("under way"));
  sum +=
      "\n" + tr("Planned %1, %2, arrival %3")
                 .arg(units.distanceText(t.planned_nm),
                      PassageService::durationText(
                          (t.planned_arrival - t.planned_departure) / 3600.0),
                      QDateTime::fromSecsSinceEpoch(t.planned_arrival)
                          .toString("HH:mm"));
  if (!t.weather.empty()) sum += "\n" + U(t.weather);
  m_summary->setText(sum);

  clearLayout(m_entries);
  const auto entries = svc().logbook().Entries(id);
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    const auto& e = *it;
    auto* row = new ListRow;
    row->setMinimumHeight(52);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(14, 6, 10, 6);
    auto* when =
        label(QDateTime::fromSecsSinceEpoch(e.time).toString("HH:mm"), "value");
    when->setMinimumWidth(48);
    rl->addWidget(when);
    QStringList sub{QStringLiteral("%1°%2 %3°%4")
                        .arg(std::fabs(e.lat), 0, 'f', 4)
                        .arg(e.lat >= 0 ? "N" : "S")
                        .arg(std::fabs(e.lon), 0, 'f', 4)
                        .arg(e.lon >= 0 ? "E" : "W"),
                    tr("%1 · %2°")
                        .arg(units.speedText(e.sog))
                        .arg(int(std::lround(e.cog)) % 360, 3, 10, QChar('0'))};
    if (e.wind_kn >= 0)
      sub << tr("wind %1 kn %2")
                 .arg(e.wind_kn, 0, 'f', 0)
                 .arg(compass(e.wind_dir));
    rl->addWidget(textColumn(e.event.empty() ? tr("Position") : U(e.event),
                             sub.join(" · ")),
                  1);
    const double lat = e.lat, lon = e.lon;
    connect(row, &ListRow::clicked, this, [this, lat, lon] {
      m_chart->centerOn(lat, lon);
      emit m_chart->navigationRequested();
    });
    m_entries->addWidget(row);
  }
  m_stack->setCurrentIndex(1);
}

void LogbookView::exportAll() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export logbook"),
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
          "/logbook.html",
      tr("HTML (*.html)"));
  if (path.isEmpty()) return;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, tr("Export logbook"), f.errorString());
    return;
  }
  // same look as the app and the seachart harbour guide
  QTextStream o(&f);
  o << "<!doctype html><html><meta charset=utf-8><title>"
    << tr("Logbook").toHtmlEscaped() << "</title><style>"
    << seachart::kHtmlStyle << "</style><header><h1>"
    << tr("Logbook").toHtmlEscaped() << "</h1></header><main>";
  for (const auto& t : svc().logbook().Trips())
    o << "<section><h2>" << U(t.name).toHtmlEscaped() << "</h2><pre>"
      << svc().tripReport(t.id).toHtmlEscaped() << "</pre></section>";
  o << "</main></html>";
}

// ------------------------------------------------------------- PassagePage

PassagePage::PassagePage(ChartView* chart, QWidget* parent) : QWidget(parent) {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(10);
  auto* tabs = new QWidget;
  auto* tl = new QHBoxLayout(tabs);
  tl->setContentsMargins(0, 0, 0, 0);
  tl->setSpacing(4);
  m_tabs = new QButtonGroup(this);
  const QStringList names{tr("Plan"), tr("Harbours"), tr("Logbook")};
  for (int i = 0; i < names.size(); ++i) {
    auto* b = new QPushButton(names[i]);
    b->setCheckable(true);
    b->setProperty("role", "segment");
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(38);
    m_tabs->addButton(b, i);
    tl->addWidget(b, 1);
  }
  l->addWidget(tabs);
  m_stack = new QStackedWidget;
  m_plan = new PlanView(chart);
  m_harbours = new HarbourView(chart);
  m_logbook = new LogbookView(chart);
  m_stack->addWidget(m_plan);
  m_stack->addWidget(m_harbours);
  m_stack->addWidget(m_logbook);
  l->addWidget(m_stack, 1);
  connect(m_tabs, &QButtonGroup::idClicked, this,
          [this](int id) { showView(View(id)); });
  connect(m_harbours, &HarbourView::planFrom, this, [this](qint64 id) {
    m_plan->setFrom(id);
    showView(Plan);
  });
  connect(m_harbours, &HarbourView::planTo, this, [this](qint64 id) {
    m_plan->setTo(id);
    showView(Plan);
  });
  connect(&Theme::instance(), &Theme::changed, this,
          [this] { refreshIcons(this); });
  showView(Plan);
}

void PassagePage::showView(View v) {
  m_tabs->button(v)->setChecked(true);
  m_stack->setCurrentIndex(v);
}
