#include "ui/SailPage.h"

#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include "core/BoatConfig.h"
#include "ui/Theme.h"

namespace {
QLabel* section(const QString& text) {
  auto* l = new QLabel(text);
  l->setProperty("role", "section");
  return l;
}

/** Row of exclusive pill buttons (segmented control). */
QWidget* segmented(QButtonGroup* group, const QStringList& items) {
  auto* w = new QWidget;
  auto* l = new QHBoxLayout(w);
  l->setContentsMargins(0, 2, 0, 6);
  l->setSpacing(4);
  for (int i = 0; i < items.size(); ++i) {
    auto* b = new QPushButton(items[i]);
    b->setCheckable(true);
    b->setProperty("role", "segment");
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(38);
    group->addButton(b, i);
    l->addWidget(b, 1);
  }
  return w;
}

QDoubleSpinBox* spin(double min, double max, double step, int decimals,
                     const QString& suffix) {
  auto* s = new QDoubleSpinBox;
  s->setRange(min, max);
  s->setSingleStep(step);
  s->setDecimals(decimals);
  s->setSuffix(suffix);
  s->setAlignment(Qt::AlignRight);
  s->setMinimumWidth(110);
  return s;
}

QWidget* row(const QString& text, QWidget* control) {
  auto* w = new QWidget;
  auto* l = new QHBoxLayout(w);
  l->setContentsMargins(4, 4, 4, 4);
  auto* lab = new QLabel(text);
  lab->setProperty("role", "value");
  l->addWidget(lab, 1);
  l->addWidget(control);
  return w;
}
}  // namespace

SailPage::SailPage(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  auto* scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* content = new QWidget;
  content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* l = new QVBoxLayout(content);
  l->setContentsMargins(0, 0, 8, 0);
  l->setSpacing(2);
  scroll->setWidget(content);
  outer->addWidget(scroll);

  m_propulsion = new QButtonGroup(this);
  m_main = new QButtonGroup(this);
  m_head = new QButtonGroup(this);
  m_downwind = new QButtonGroup(this);
  using S = SailConfig;
  l->addWidget(section(tr("PROPULSION")));
  l->addWidget(segmented(m_propulsion, {S::name(S::Propulsion::Sailing),
                                        S::name(S::Propulsion::MotorSailing),
                                        S::name(S::Propulsion::Motor)}));
  l->addWidget(section(tr("MAINSAIL")));
  l->addWidget(
      segmented(m_main, {S::name(S::Main::Down), S::name(S::Main::Full),
                         tr("Reef 1"), tr("Reef 2"), tr("Reef 3")}));
  l->addWidget(section(tr("HEADSAIL")));
  l->addWidget(
      segmented(m_head, {S::name(S::Head::None), S::name(S::Head::Genoa),
                         S::name(S::Head::Jib), S::name(S::Head::Storm)}));
  m_furl = new QSlider(Qt::Horizontal);
  m_furl->setRange(20, 100);
  m_furl->setSingleStep(5);
  m_furl->setPageStep(10);
  m_furlLabel = new QLabel;
  m_furlLabel->setProperty("role", "value");
  m_furlLabel->setMinimumWidth(48);
  m_furlRow = new QWidget;
  auto* fl = new QHBoxLayout(m_furlRow);
  fl->setContentsMargins(4, 0, 4, 6);
  auto* fname = new QLabel(tr("Unfurled"));
  fname->setProperty("role", "dim");
  fl->addWidget(fname);
  fl->addWidget(m_furl, 1);
  fl->addWidget(m_furlLabel);
  l->addWidget(m_furlRow);
  l->addWidget(section(tr("LIGHT WIND SAIL")));
  l->addWidget(segmented(
      m_downwind,
      {S::name(S::Downwind::None), S::name(S::Downwind::Gennaker),
       S::name(S::Downwind::Spinnaker), S::name(S::Downwind::Code0)}));

  l->addWidget(section(tr("ENGINE AND TANK")));
  m_tank = spin(5, 5000, 10, 0, " l");
  m_consumption = spin(0.1, 200, 0.1, 1, " l/h");
  m_cruise = spin(500, 6000, 100, 0, " rpm");
  m_fuel = spin(0, 100, 5, 0, " %");
  l->addWidget(row(tr("Tank capacity"), m_tank));
  l->addWidget(row(tr("Consumption at cruise"), m_consumption));
  l->addWidget(row(tr("Cruise RPM"), m_cruise));
  l->addWidget(row(tr("Fuel level (without sensor)"), m_fuel));
  auto* hint = new QLabel(
      tr("The sail plan is shown in the cockpit view. Engine values come from "
         "NMEA 0183 RPM and XDR sentences; the range is computed from the "
         "fuel level, consumption and speed over ground."));
  hint->setWordWrap(true);
  hint->setProperty("role", "dim");
  hint->setContentsMargins(4, 10, 4, 4);
  l->addWidget(hint);
  l->addStretch(1);

  sync();
  for (QButtonGroup* g : {m_propulsion, m_main, m_head, m_downwind})
    connect(g, &QButtonGroup::idClicked, this, &SailPage::apply);
  connect(m_furl, &QSlider::valueChanged, this, &SailPage::apply);
  for (QDoubleSpinBox* s : {m_tank, m_consumption, m_cruise, m_fuel})
    connect(s, &QDoubleSpinBox::valueChanged, this, &SailPage::apply);
  connect(&BoatConfig::instance(), &BoatConfig::changed, this, &SailPage::sync);
}

void SailPage::sync() {
  m_syncing = true;
  const auto& b = BoatConfig::instance();
  const SailConfig& s = b.sails();
  m_propulsion->button(int(s.propulsion))->setChecked(true);
  m_main->button(int(s.main))->setChecked(true);
  m_head->button(int(s.head))->setChecked(true);
  m_downwind->button(int(s.downwind))->setChecked(true);
  m_furl->setValue(s.headFurlPct);
  m_furlLabel->setText(QStringLiteral("%1%").arg(s.headFurlPct));
  m_furlRow->setVisible(s.head == SailConfig::Head::Genoa);
  m_tank->setValue(b.tankLiters);
  m_consumption->setValue(b.consumptionLph);
  m_cruise->setValue(b.cruiseRpm);
  m_fuel->setValue(b.manualFuelPct);
  m_syncing = false;
}

void SailPage::apply() {
  if (m_syncing) return;
  SailConfig s;
  s.propulsion = SailConfig::Propulsion(m_propulsion->checkedId());
  s.main = SailConfig::Main(m_main->checkedId());
  s.head = SailConfig::Head(m_head->checkedId());
  s.headFurlPct = m_furl->value();
  s.downwind = SailConfig::Downwind(m_downwind->checkedId());
  auto& b = BoatConfig::instance();
  const bool engineChanged = b.tankLiters != m_tank->value() ||
                             b.consumptionLph != m_consumption->value() ||
                             b.cruiseRpm != m_cruise->value() ||
                             b.manualFuelPct != m_fuel->value();
  if (engineChanged) {
    m_syncing = true;  // setEngineSetup emits changed()
    b.setEngineSetup(m_tank->value(), m_consumption->value(), m_cruise->value(),
                     m_fuel->value());
    m_syncing = false;
  }
  b.setSails(s);
}
