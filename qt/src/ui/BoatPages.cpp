#include "ui/BoatPages.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <cmath>

#include "ui/DrawerPages.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
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
  const QColor c =
      role && (QByteArray(role) == "primary" || QByteArray(role) == "danger")
          ? QColor(Qt::white)
          : Theme::p().text;
  if (!icon.isEmpty()) b->setIcon(Icons::icon(icon, c, 18));
  b->setIconSize(QSize(18, 18));
  return b;
}

QLabel* iconLabel(const QString& icon, const QColor& color, int size = 22) {
  auto* l = new QLabel;
  l->setFixedSize(size + 4, size + 4);
  l->setAlignment(Qt::AlignCenter);
  l->setPixmap(Icons::pixmap(icon, color, size));
  return l;
}

/** Scroll area whose content follows the drawer width. */
QScrollArea* scrollFor(QWidget* content) {
  auto* scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  scroll->setWidget(content);
  return scroll;
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

QString zoneName(const QString& id) {
  const BoatZone* z = BoatSystems::instance().zone(id);
  return z ? z->name : id;
}

const QStringList kSceneIcons = {"bulb", "moon",    "sun",  "anchor",
                                 "drop", "music",   "heat", "palette",
                                 "eye",  "compass", "close"};
const QStringList kAutomationIcons = {"auto", "drop",   "sun",  "moon",
                                      "heat", "anchor", "bell", "close"};

void fillIcons(QComboBox* c, const QStringList& icons) {
  for (const QString& i : icons)
    c->addItem(Icons::icon(i, Theme::p().text, 18), QString(), i);
  c->setIconSize(QSize(18, 18));
  c->setFixedWidth(76);
}
}  // namespace

// --------------------------------------------------------------------------
// ColorButton

ColorButton::ColorButton(QWidget* parent)
    : QWidget(parent), m_color(255, 236, 200) {
  setCursor(Qt::PointingHandCursor);
  setFixedSize(sizeHint());
  setToolTip(tr("LED color"));
}

QVector<QPair<QString, QColor>> ColorButton::presets() {
  return {{tr("Warm white"), QColor(255, 176, 110)},
          {tr("Neutral white"), QColor(255, 236, 200)},
          {tr("Cool white"), QColor(225, 238, 255)},
          {tr("Night red"), QColor(255, 28, 0)},
          {tr("Amber"), QColor(255, 140, 20)},
          {tr("Blue"), QColor(40, 110, 255)},
          {tr("Turquoise"), QColor(0, 200, 190)},
          {tr("Green"), QColor(40, 210, 90)},
          {tr("Violet"), QColor(160, 80, 255)}};
}

void ColorButton::setColor(const QColor& c) {
  if (!c.isValid() || c == m_color) return;
  m_color = c;
  update();
  emit colorChanged(c);
}

void ColorButton::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = QRectF(rect()).adjusted(3, 3, -3, -3);
  QColor glow = m_color;
  glow.setAlpha(isEnabled() ? 90 : 30);
  p.setPen(Qt::NoPen);
  p.setBrush(glow);
  p.drawEllipse(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
  QColor c = m_color;
  if (!isEnabled()) c.setAlpha(80);
  p.setBrush(c);
  p.setPen(QPen(Theme::p().panelBorder, 1));
  p.drawEllipse(r);
}

void ColorButton::mouseReleaseEvent(QMouseEvent* e) {
  if (!rect().contains(e->position().toPoint())) return;
  QMenu menu(this);
  for (const auto& [name, color] : presets()) {
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    {
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      p.setBrush(color);
      p.setPen(Qt::NoPen);
      p.drawEllipse(QRectF(1, 1, 16, 16));
    }
    QAction* a = menu.addAction(QIcon(pm), name);
    connect(a, &QAction::triggered, this, [this, c = color] { setColor(c); });
  }
  menu.addSeparator();
  connect(
      menu.addAction(tr("Custom color…")), &QAction::triggered, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, this, tr("LED color"));
        if (c.isValid()) setColor(c);
      });
  menu.exec(mapToGlobal(QPoint(0, height())));
}

// --------------------------------------------------------------------------
// SceneEditor

SceneEditor::SceneEditor(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(10);

  auto* head = new QHBoxLayout;
  m_icon = new QComboBox;
  fillIcons(m_icon, kSceneIcons);
  head->addWidget(m_icon);
  m_name = new QLineEdit;
  m_name->setPlaceholderText(tr("Scene name"));
  head->addWidget(m_name, 1);
  outer->addLayout(head);

  auto* opts = new QHBoxLayout;
  m_preview = new QCheckBox(tr("Live preview on board"));
  m_preview->setChecked(true);
  connect(m_preview, &QCheckBox::toggled, this, [this](bool on) {
    if (on) {
      preview();
    } else {
      // Back to the lights as they were.
      auto& sys = BoatSystems::instance();
      for (const BoatLight& l : m_before) {
        sys.setLightLevel(l.id, l.brightness, l.color);
        sys.setLight(l.id, l.on);
      }
    }
  });
  opts->addWidget(m_preview, 1);
  auto* all = new ColorButton;
  all->setToolTip(tr("One color for all lights of the scene"));
  connect(all, &ColorButton::colorChanged, this, &SceneEditor::setAllColors);
  opts->addWidget(label(tr("All"), "dim"));
  opts->addWidget(all);
  outer->addLayout(opts);

  auto* content = new QWidget;
  m_rows = new QVBoxLayout(content);
  m_rows->setContentsMargins(0, 0, 8, 0);
  m_rows->setSpacing(4);
  outer->addWidget(scrollFor(content), 1);

  auto* buttons = new QHBoxLayout;
  m_delete = button(tr("Delete"), "trash", "ghost");
  connect(m_delete, &QPushButton::clicked, this, [this] {
    if (m_scene.id.isEmpty()) return;
    BoatSystems::instance().removeScene(m_scene.id);
    finish();
  });
  buttons->addWidget(m_delete);
  buttons->addStretch(1);
  auto* cancel = button(tr("Cancel"), QString());
  connect(cancel, &QPushButton::clicked, this, &SceneEditor::finish);
  buttons->addWidget(cancel);
  auto* save = button(tr("Save"), "check", "primary");
  connect(save, &QPushButton::clicked, this, [this] {
    LightScene s = scene();
    if (s.name.trimmed().isEmpty()) s.name = tr("My scene");
    auto& sys = BoatSystems::instance();
    sys.saveScene(s);
    // The id of a new scene is made by the store: the last one added.
    const QString id = s.id.isEmpty() ? sys.scenes().last().id : s.id;
    m_before.clear();  // keep the lights as previewed
    sys.applyScene(id);
    emit saved(id);
    emit closed();
  });
  buttons->addWidget(save);
  outer->addLayout(buttons);
}

void SceneEditor::edit(const LightScene& scene) {
  m_loading = true;
  m_scene = scene;
  m_before = BoatSystems::instance().lights();
  m_name->setText(scene.name);
  const int icon = m_icon->findData(scene.icon);
  m_icon->setCurrentIndex(icon >= 0 ? icon : 0);
  m_delete->setVisible(!scene.id.isEmpty());
  m_delete->setText(scene.builtIn ? tr("Reset") : tr("Delete"));
  m_delete->setIcon(
      Icons::icon(scene.builtIn ? "refresh" : "trash", Theme::p().textDim, 18));

  clearLayout(m_rows);
  m_lightRows.clear();
  QString zone;
  for (const BoatLight& l : BoatSystems::instance().lights()) {
    if (l.zone != zone) {
      zone = l.zone;
      m_rows->addWidget(label(
          l.navigation ? tr("NAVIGATION & DECK") : zoneName(zone).toUpper(),
          "section"));
    }
    const SceneLight* sl = scene.find(l.id);
    auto* w = new QWidget;
    w->setObjectName("sceneLight-" + l.id);
    auto* g = new QGridLayout(w);
    g->setContentsMargins(4, 2, 4, 6);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(2);
    Row row;
    row.lightId = l.id;
    row.include = new QCheckBox(l.name);
    row.include->setToolTip(
        tr("Part of the scene; lights not included keep their state"));
    row.include->setChecked(sl != nullptr);
    row.on = new ToggleSwitch;
    row.on->setChecked(sl ? sl->on : l.on);
    row.level = new QSlider(Qt::Horizontal);
    row.level->setRange(1, 100);
    row.level->setValue(
        int(std::round((sl ? sl->brightness : l.brightness) * 100)));
    row.percent = label(QString(), "dim");
    row.percent->setMinimumWidth(40);
    row.percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.color = new ColorButton;
    row.color->setColor(sl ? sl->color : l.color);
    g->addWidget(row.include, 0, 0, 1, 2);
    g->addWidget(row.on, 0, 2, Qt::AlignRight);
    g->addWidget(row.level, 1, 0);
    g->addWidget(row.percent, 1, 1);
    g->addWidget(row.color, 1, 2, Qt::AlignRight);
    g->setColumnStretch(0, 1);
    m_rows->addWidget(w);

    const auto update = [this, r = row] {
      const bool inc = r.include->isChecked();
      r.on->setEnabled(inc);
      r.level->setEnabled(inc && r.on->isChecked());
      r.color->setEnabled(inc && r.on->isChecked());
      r.percent->setText(QStringLiteral("%1 %").arg(r.level->value()));
      if (!m_loading) preview();
    };
    connect(row.include, &QCheckBox::toggled, this, update);
    connect(row.on, &QAbstractButton::toggled, this, update);
    connect(row.level, &QSlider::valueChanged, this, update);
    connect(row.color, &ColorButton::colorChanged, this, update);
    m_lightRows << row;
    update();
  }
  m_rows->addStretch(1);
  m_loading = false;
  preview();
}

LightScene SceneEditor::scene() const {
  LightScene s = m_scene;
  s.name = m_name->text().trimmed();
  s.icon = m_icon->currentData().toString();
  s.lights.clear();
  for (const Row& r : m_lightRows) {
    if (!r.include->isChecked()) continue;
    s.lights << SceneLight{r.lightId, r.on->isChecked(),
                           r.level->value() / 100.0, r.color->color()};
  }
  return s;
}

void SceneEditor::setAllColors(const QColor& c) {
  m_loading = true;
  for (const Row& r : m_lightRows)
    if (r.include->isChecked()) r.color->setColor(c);
  m_loading = false;
  preview();
}

void SceneEditor::preview() {
  if (m_loading || !m_preview->isChecked()) return;
  auto& sys = BoatSystems::instance();
  for (const SceneLight& sl : scene().lights) {
    sys.setLightLevel(sl.lightId, sl.brightness, sl.color);
    sys.setLight(sl.lightId, sl.on);
  }
}

void SceneEditor::finish() {
  auto& sys = BoatSystems::instance();
  for (const BoatLight& l : m_before) {
    sys.setLightLevel(l.id, l.brightness, l.color);
    sys.setLight(l.id, l.on);
  }
  m_before.clear();
  emit closed();
}

// --------------------------------------------------------------------------
// LightScenesPage

LightScenesPage::LightScenesPage(QWidget* parent) : QWidget(parent) {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  m_stack = new QStackedWidget;
  l->addWidget(m_stack);

  auto* listPage = new QWidget;
  auto* ll = new QVBoxLayout(listPage);
  ll->setContentsMargins(0, 0, 0, 0);
  ll->setSpacing(10);
  auto* hint = label(tr("A scene sets brightness and color of the LEDs it "
                        "contains. Tap a scene to switch it on."),
                     "dim");
  hint->setWordWrap(true);
  ll->addWidget(hint);
  auto* actions = new QHBoxLayout;
  auto* add = button(tr("New scene"), "plus", "primary");
  add->setToolTip(tr("Starts from the lights as they are now"));
  connect(add, &QPushButton::clicked, this, [this] { editScene({}); });
  actions->addWidget(add);
  actions->addStretch(1);
  ll->addLayout(actions);
  auto* content = new QWidget;
  m_list = new QVBoxLayout(content);
  m_list->setContentsMargins(0, 0, 8, 0);
  m_list->setSpacing(8);
  ll->addWidget(scrollFor(content), 1);
  m_stack->addWidget(listPage);

  m_editor = new SceneEditor;
  m_stack->addWidget(m_editor);
  connect(m_editor, &SceneEditor::closed, this,
          [this] { m_stack->setCurrentIndex(0); });

  auto& sys = BoatSystems::instance();
  connect(&sys, &BoatSystems::scenesChanged, this, &LightScenesPage::rebuild);
  connect(&sys, &BoatSystems::changed, this, [this] {
    // Rebuild only when the active scene changes (the marker).
    const QString active = BoatSystems::instance().activeScene();
    if (active != property("shownScene").toString()) rebuild();
  });
  connect(&Theme::instance(), &Theme::changed, this, &LightScenesPage::rebuild);
  rebuild();
}

void LightScenesPage::editScene(const QString& id) {
  auto& sys = BoatSystems::instance();
  const LightScene* s = id.isEmpty() ? nullptr : sys.scene(id);
  m_editor->edit(s ? *s : sys.captureScene(tr("My scene")));
  m_stack->setCurrentIndex(1);
}

void LightScenesPage::rebuild() {
  clearLayout(m_list);
  auto& sys = BoatSystems::instance();
  setProperty("shownScene", sys.activeScene());
  const Palette& pal = Theme::p();
  for (const LightScene& s : sys.scenes()) {
    auto* row = new ListRow;
    row->setObjectName("scene-" + s.id);
    const bool active = s.id == sys.activeScene();
    if (active) row->setHighlight(pal.accentBright);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 8, 10, 8);
    l->setSpacing(10);
    l->addWidget(iconLabel(
        s.icon, s.id == "night" ? QColor(255, 60, 40) : pal.accentBright));
    auto* col = new QVBoxLayout;
    col->setSpacing(4);
    auto* name = label(s.name, "value");
    col->addWidget(name);
    // Color dots of the lights that are on.
    auto* dots = new QLabel;
    int on = 0;
    QPixmap pm(16 * 12, 14);
    pm.fill(Qt::transparent);
    {
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      for (const SceneLight& sl : s.lights) {
        if (!sl.on || on >= 12) continue;
        QColor c = sl.color;
        c.setAlphaF(0.35 + 0.65 * sl.brightness);
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(on * 16 + 1, 1, 12, 12));
        ++on;
      }
    }
    dots->setPixmap(pm);
    auto* dl = new QHBoxLayout;
    dl->setSpacing(6);
    if (on > 0) dl->addWidget(dots);
    dl->addWidget(label(on == 0   ? tr("all lights off")
                        : on == 1 ? tr("1 light on")
                                  : tr("%1 lights on").arg(on),
                        "dim"),
                  1);
    col->addLayout(dl);
    l->addLayout(col, 1);
    auto* edit = new IconButton("edit", tr("Edit scene"));
    connect(edit, &QAbstractButton::clicked, this,
            [this, id = s.id] { editScene(id); });
    l->addWidget(edit);
    connect(row, &ListRow::clicked, this,
            [id = s.id] { BoatSystems::instance().applyScene(id); });
    m_list->addWidget(row);
  }
  m_list->addStretch(1);
}

// --------------------------------------------------------------------------
// AutomationEditor

AutomationEditor::AutomationEditor(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(10);
  auto* content = new QWidget;
  auto* l = new QVBoxLayout(content);
  l->setContentsMargins(0, 0, 8, 0);
  l->setSpacing(8);
  outer->addWidget(scrollFor(content), 1);

  auto* head = new QHBoxLayout;
  m_icon = new QComboBox;
  fillIcons(m_icon, kAutomationIcons);
  head->addWidget(m_icon);
  m_name = new QLineEdit;
  m_name->setPlaceholderText(tr("Name, e.g. Winter shower"));
  head->addWidget(m_name, 1);
  l->addLayout(head);

  l->addWidget(label(tr("WHEN"), "section"));
  auto* trow = new QHBoxLayout;
  m_trigger = new QComboBox;
  m_trigger->addItems({tr("On tap"), tr("At a time of day")});
  trow->addWidget(m_trigger, 1);
  m_timeBox = new QWidget;
  auto* tl = new QHBoxLayout(m_timeBox);
  tl->setContentsMargins(0, 0, 0, 0);
  m_time = new QTimeEdit;
  m_time->setDisplayFormat("HH:mm");
  tl->addWidget(m_time);
  trow->addWidget(m_timeBox);
  l->addLayout(trow);
  connect(m_trigger, &QComboBox::currentIndexChanged, this,
          &AutomationEditor::updateTrigger);

  auto* days = new QHBoxLayout;
  days->setSpacing(4);
  for (int i = 0; i < 7; ++i) {
    m_days[i] =
        new QPushButton(QLocale().dayName(i + 1, QLocale::NarrowFormat));
    m_days[i]->setProperty("role", "segment");
    m_days[i]->setCheckable(true);
    m_days[i]->setMinimumWidth(36);
    m_days[i]->setToolTip(QLocale().dayName(i + 1));
    days->addWidget(m_days[i]);
  }
  auto* dayBox = new QWidget;
  dayBox->setObjectName("days");
  dayBox->setLayout(days);
  l->addWidget(dayBox);

  l->addWidget(label(tr("MONTHS"), "section"));
  auto* seasons = new QHBoxLayout;
  const struct {
    QString name;
    int mask;
  } presets[] = {{tr("All year"), 0xfff},
                 {tr("Winter"), Automation::kWinter},
                 {tr("Summer"), 0xfff & ~Automation::kWinter}};
  for (const auto& p : presets) {
    auto* b = button(p.name, QString(), "ghost");
    connect(b, &QPushButton::clicked, this, [this, mask = p.mask] {
      for (int i = 0; i < 12; ++i) m_months[i]->setChecked(mask & (1 << i));
    });
    seasons->addWidget(b);
  }
  seasons->addStretch(1);
  l->addLayout(seasons);
  auto* months = new QGridLayout;
  months->setSpacing(4);
  for (int i = 0; i < 12; ++i) {
    m_months[i] =
        new QPushButton(QLocale().monthName(i + 1, QLocale::ShortFormat));
    m_months[i]->setProperty("role", "segment");
    m_months[i]->setCheckable(true);
    months->addWidget(m_months[i], i / 6, i % 6);
  }
  l->addLayout(months);

  l->addWidget(label(tr("ACTIONS"), "section"));
  m_actions = new QVBoxLayout;
  m_actions->setSpacing(8);
  l->addLayout(m_actions);
  auto* add = button(tr("Add action"), "plus");
  connect(add, &QPushButton::clicked, this, [this] {
    AutomationAction a;
    a.type = AutomationAction::Type::Scene;
    addAction(a);
  });
  l->addWidget(add, 0, Qt::AlignLeft);
  l->addStretch(1);

  auto* buttons = new QHBoxLayout;
  m_delete = button(tr("Delete"), "trash", "ghost");
  connect(m_delete, &QPushButton::clicked, this, [this] {
    if (!m_automation.id.isEmpty())
      AutomationStore::instance().remove(m_automation.id);
    emit closed();
  });
  buttons->addWidget(m_delete);
  buttons->addStretch(1);
  auto* cancel = button(tr("Cancel"), QString());
  connect(cancel, &QPushButton::clicked, this, &AutomationEditor::closed);
  buttons->addWidget(cancel);
  auto* save = button(tr("Save"), "check", "primary");
  connect(save, &QPushButton::clicked, this, [this] {
    Automation a = automation();
    if (a.name.isEmpty()) a.name = tr("Automation");
    const QString id = AutomationStore::instance().save(a);
    emit saved(id);
    emit closed();
  });
  buttons->addWidget(save);
  outer->addLayout(buttons);
}

void AutomationEditor::updateTrigger() {
  const bool timed = m_trigger->currentIndex() == 1;
  m_timeBox->setVisible(timed);
  if (auto* d = findChild<QWidget*>("days")) d->setVisible(timed);
}

void AutomationEditor::edit(const Automation& a) {
  m_automation = a;
  m_name->setText(a.name);
  const int icon = m_icon->findData(a.icon);
  m_icon->setCurrentIndex(icon >= 0 ? icon : 0);
  m_trigger->setCurrentIndex(a.trigger == Automation::Trigger::Time ? 1 : 0);
  m_time->setTime(a.time);
  for (int i = 0; i < 7; ++i) m_days[i]->setChecked(a.weekdays & (1 << i));
  for (int i = 0; i < 12; ++i) m_months[i]->setChecked(a.months & (1 << i));
  m_delete->setVisible(!a.id.isEmpty());
  clearLayout(m_actions);
  m_actionRows.clear();
  for (const auto& act : a.actions) addAction(act);
  updateTrigger();
}

void AutomationEditor::fillTargets(ActionRow& row, AutomationAction::Type t,
                                   const QString& current) {
  using T = AutomationAction::Type;
  auto& sys = BoatSystems::instance();
  const QSignalBlocker block(row.target);
  row.target->clear();
  switch (t) {
    case T::ZoneSetpoint:
      for (const auto& z : sys.zones())
        if (z.id != "outside") row.target->addItem(z.name, z.id);
      break;
    case T::Scene:
      for (const auto& s : sys.scenes()) row.target->addItem(s.name, s.id);
      break;
    case T::Light:
      for (const auto& l : sys.lights())
        row.target->addItem(
            l.navigation ? l.name : zoneName(l.zone) + " · " + l.name, l.id);
      break;
    case T::Consumer:
      for (const auto& c : sys.consumers())
        if (c.switchable) row.target->addItem(c.name, c.id);
      break;
    default:
      break;
  }
  const int i = row.target->findData(current);
  if (i >= 0) row.target->setCurrentIndex(i);
  row.target->setVisible(row.target->count() > 0);
  row.value->setVisible(t == T::ZoneSetpoint);
  row.on->setVisible(t != T::ZoneSetpoint && t != T::Scene);
}

void AutomationEditor::addAction(const AutomationAction& a) {
  using T = AutomationAction::Type;
  auto* w = new QWidget;
  w->setObjectName("automationAction");
  w->setAttribute(Qt::WA_StyledBackground);
  w->setStyleSheet(
      QStringLiteral(
          "#automationAction { background: %1; border-radius: 14px; }")
          .arg(Theme::p().panelHover.name(QColor::HexArgb)));
  auto* g = new QGridLayout(w);
  g->setContentsMargins(10, 8, 8, 8);
  g->setSpacing(6);
  ActionRow row;
  row.widget = w;
  row.type = new QComboBox;
  for (int i = 0; i <= int(T::Consumer); ++i)
    row.type->addItem(AutomationAction::typeName(T(i)), i);
  row.type->setCurrentIndex(int(a.type));
  row.target = new QComboBox;
  row.value = new QDoubleSpinBox;
  row.value->setRange(8, 30);
  row.value->setSingleStep(0.5);
  row.value->setDecimals(1);
  row.value->setSuffix(" °C");
  row.value->setAlignment(Qt::AlignRight);
  row.value->setValue(a.type == T::ZoneSetpoint ? a.value : 20);
  row.on = new ToggleSwitch;
  row.on->setChecked(a.on);
  auto* del = new IconButton("trash", tr("Remove action"), nullptr, true);
  g->addWidget(row.type, 0, 0, 1, 2);
  g->addWidget(del, 0, 2);
  g->addWidget(row.target, 1, 0);
  auto* vb = new QHBoxLayout;
  vb->setSpacing(0);
  vb->addWidget(row.value);
  vb->addWidget(row.on);
  g->addLayout(vb, 1, 1, 1, 2, Qt::AlignRight);
  g->setColumnStretch(0, 1);
  m_actions->addWidget(w);
  m_actionRows << row;
  fillTargets(m_actionRows.last(), a.type, a.target);

  connect(row.type, &QComboBox::currentIndexChanged, this, [this, w](int i) {
    for (auto& r : m_actionRows)
      if (r.widget == w)
        fillTargets(r, T(i), r.target->currentData().toString());
  });
  connect(del, &QAbstractButton::clicked, this, [this, w] {
    for (int i = 0; i < m_actionRows.size(); ++i)
      if (m_actionRows[i].widget == w) m_actionRows.removeAt(i);
    w->hide();
    w->deleteLater();
  });
}

Automation AutomationEditor::automation() const {
  Automation a = m_automation;
  a.name = m_name->text().trimmed();
  a.icon = m_icon->currentData().toString();
  a.trigger = m_trigger->currentIndex() == 1 ? Automation::Trigger::Time
                                             : Automation::Trigger::Manual;
  a.time = m_time->time();
  a.weekdays = 0;
  for (int i = 0; i < 7; ++i)
    if (m_days[i]->isChecked()) a.weekdays |= 1 << i;
  a.months = 0;
  for (int i = 0; i < 12; ++i)
    if (m_months[i]->isChecked()) a.months |= 1 << i;
  a.actions.clear();
  for (const auto& r : m_actionRows) {
    AutomationAction act;
    act.type = AutomationAction::Type(r.type->currentData().toInt());
    act.target = r.target->currentData().toString();
    act.value = r.value->value();
    act.on = r.on->isChecked();
    a.actions << act;
  }
  return a;
}

// --------------------------------------------------------------------------
// AutomationsPage

AutomationsPage::AutomationsPage(QWidget* parent) : QWidget(parent) {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  m_stack = new QStackedWidget;
  l->addWidget(m_stack);

  auto* listPage = new QWidget;
  auto* ll = new QVBoxLayout(listPage);
  ll->setContentsMargins(0, 0, 0, 0);
  ll->setSpacing(10);
  auto* hint = label(tr("An automation switches several systems at once: on "
                        "a tap or at a time of day, optionally only on some "
                        "weekdays or in winter."),
                     "dim");
  hint->setWordWrap(true);
  ll->addWidget(hint);
  auto* actions = new QHBoxLayout;
  auto* add = button(tr("New automation"), "plus", "primary");
  connect(add, &QPushButton::clicked, this, [this] { editAutomation({}); });
  actions->addWidget(add);
  actions->addStretch(1);
  ll->addLayout(actions);
  auto* content = new QWidget;
  m_list = new QVBoxLayout(content);
  m_list->setContentsMargins(0, 0, 8, 0);
  m_list->setSpacing(8);
  ll->addWidget(scrollFor(content), 1);
  m_stack->addWidget(listPage);

  m_editor = new AutomationEditor;
  m_stack->addWidget(m_editor);
  connect(m_editor, &AutomationEditor::closed, this,
          [this] { m_stack->setCurrentIndex(0); });
  connect(&AutomationStore::instance(), &AutomationStore::changed, this,
          &AutomationsPage::rebuild);
  connect(&BoatSystems::instance(), &BoatSystems::scenesChanged, this,
          &AutomationsPage::rebuild);
  connect(&Theme::instance(), &Theme::changed, this, &AutomationsPage::rebuild);
  rebuild();
}

void AutomationsPage::editAutomation(const QString& id) {
  const Automation* a = AutomationStore::instance().find(id);
  Automation fresh;
  if (!a) {
    fresh.name = QString();
    AutomationAction act;
    act.type = AutomationAction::Type::Heater;
    fresh.actions << act;
  }
  m_editor->edit(a ? *a : fresh);
  m_stack->setCurrentIndex(1);
}

void AutomationsPage::rebuild() {
  clearLayout(m_list);
  const Palette& pal = Theme::p();
  for (const Automation& a : AutomationStore::instance().automations()) {
    auto* row = new ListRow;
    row->setObjectName("automation-" + a.id);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 8, 10, 8);
    l->setSpacing(10);
    l->addWidget(iconLabel(a.icon, a.enabled ? pal.accentBright : pal.textDim));
    auto* col = new QVBoxLayout;
    col->setSpacing(2);
    col->addWidget(label(a.name, "value"));
    QStringList what;
    for (const auto& act : a.actions) what << act.describe();
    auto* sched = label(a.schedule(), "dim");
    col->addWidget(sched);
    auto* acts = label(what.join(" · "), "dim");
    acts->setWordWrap(true);
    col->addWidget(acts);
    l->addLayout(col, 1);
    auto* v = new QVBoxLayout;
    v->setSpacing(6);
    auto* run = new IconButton("play", tr("Run now"));
    run->setObjectName("run");
    connect(run, &QAbstractButton::clicked, this,
            [id = a.id] { AutomationStore::instance().run(id); });
    v->addWidget(run);
    if (a.trigger == Automation::Trigger::Time) {
      auto* sw = new ToggleSwitch;
      sw->setToolTip(tr("Scheduled run enabled"));
      sw->setChecked(a.enabled);
      connect(sw, &QAbstractButton::toggled, this, [id = a.id](bool on) {
        auto& store = AutomationStore::instance();
        if (const Automation* x = store.find(id)) {
          Automation copy = *x;
          copy.enabled = on;
          QMetaObject::invokeMethod(
              &store, [copy] { AutomationStore::instance().save(copy); },
              Qt::QueuedConnection);
        }
      });
      v->addWidget(sw);
    }
    l->addLayout(v);
    connect(row, &ListRow::clicked, this,
            [this, id = a.id] { editAutomation(id); });
    m_list->addWidget(row);
  }
  m_list->addStretch(1);
}
