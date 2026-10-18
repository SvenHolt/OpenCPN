#include "ui/VoyageList.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QVBoxLayout>

#include "core/AppSettings.h"
#include "core/Voyage.h"
#include "ui/DrawerPages.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/OfflinePage.h"
#include "ui/Theme.h"

namespace {
QLabel* label(const QString& text, const char* role = nullptr) {
  auto* l = new QLabel(text);
  if (role) l->setProperty("role", role);
  l->setWordWrap(true);
  return l;
}
}  // namespace

VoyageList::VoyageList(QWidget* parent) : QWidget(parent) {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(8);
  l->addWidget(label(tr("VOYAGES (OFFLINE)"), "section"));
  m_rows = new QVBoxLayout;
  m_rows->setSpacing(8);
  l->addLayout(m_rows);
  auto& store = VoyageStore::instance();
  connect(&store, &VoyageStore::changed, this, &VoyageList::rebuild);
  connect(&store, &VoyageStore::progress, this,
          [this](const QString& id, const QString& text, int, int) {
            m_progressId = id;
            m_progressText = text;
            rebuild();
          });
  connect(&store, &VoyageStore::prepared, this,
          [this](const QString&, bool, const QString&) {
            m_progressId.clear();
            rebuild();
          });
  connect(&Theme::instance(), &Theme::changed, this, &VoyageList::rebuild);
  rebuild();
}

void VoyageList::rebuild() {
  while (QLayoutItem* it = m_rows->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      w->hide();
      w->deleteLater();
    }
    delete it;
  }
  const Palette& pal = Theme::p();
  const auto& voyages = VoyageStore::instance().voyages();
  setVisible(!voyages.isEmpty());
  for (const Voyage& v : voyages) {
    auto* row = new ListRow;
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 10, 10, 10);
    auto* icon = new QLabel;
    icon->setPixmap(Icons::pixmap("download", pal.success, 22));
    l->addWidget(icon);
    auto* col = new QVBoxLayout;
    col->setSpacing(2);
    auto* name = label(v.name, "value");
    name->setTextFormat(Qt::PlainText);
    col->addWidget(name);
    QStringList facts{AppSettings::instance().distanceText(v.lengthNm())};
    if (!v.forecasts.isEmpty())
      facts << tr("%n model(s)", nullptr, int(v.forecasts.size()));
    if (!v.gribFile.isEmpty()) facts << tr("GRIB");
    if (v.tiles) facts << tr("%n harbour tile(s)", nullptr, v.tiles);
    facts << QLocale().toString(v.updated.toLocalTime(), QLocale::ShortFormat);
    col->addWidget(label(facts.join(" · "), "dim"));
    if (v.id == m_progressId) {
      auto* p = label(m_progressText);
      p->setStyleSheet(
          QStringLiteral("color: %1;").arg(pal.accentBright.name()));
      col->addWidget(p);
    }
    l->addLayout(col, 1);
    const QString id = v.id;
    connect(row, &ListRow::clicked, this,
            [this, id] { emit openRequested(id); });
    auto* refresh = new IconButton("refresh", tr("Update offline data"));
    refresh->setEnabled(m_progressId.isEmpty());
    connect(refresh, &QAbstractButton::clicked, this, [id] {
      VoyageStore::instance().prepareOffline(id, OsmPoiLayer::queryTemplate());
    });
    auto* del = new IconButton("trash", tr("Delete"), nullptr, true);
    connect(del, &QAbstractButton::clicked, this, [this, id] {
      if (QMessageBox::question(this, tr("Delete voyage"),
                                tr("Delete this stored voyage?")) ==
          QMessageBox::Yes)
        VoyageStore::instance().remove(id);
    });
    l->addWidget(refresh);
    l->addWidget(del);
    m_rows->addWidget(row);
  }
}
