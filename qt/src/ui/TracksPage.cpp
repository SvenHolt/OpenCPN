#include "ui/TracksPage.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "core/AppSettings.h"
#include "core/GeoMath.h"
#include "core/GpxIO.h"
#include "core/TrackRecorder.h"
#include "ui/ChartView.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
QString durationText(qint64 secs) {
  if (secs < 3600) return QObject::tr("%1 min").arg(secs / 60);
  return QStringLiteral("%1:%2 h")
      .arg(secs / 3600)
      .arg((secs % 3600) / 60, 2, 10, QChar('0'));
}
}  // namespace

TrackLayer::TrackLayer(QObject* parent) : MapLayer(parent) {
  auto& rec = TrackRecorder::instance();
  connect(&rec, &TrackRecorder::activeTrackChanged, this, &MapLayer::changed);
  connect(&rec, &TrackRecorder::tracksChanged, this, &MapLayer::changed);
}

void TrackLayer::paint(QPainter& p, const ChartView& view) {
  const Palette& pal = Theme::p();
  auto& rec = TrackRecorder::instance();
  p.setRenderHint(QPainter::Antialiasing);
  auto polyline = [&](const Route& t) {
    QPolygonF pts;
    pts.reserve(t.points.size());
    for (const auto& w : t.points) pts << view.screenPos(w.lat, w.lon);
    return pts;
  };
  for (const Route& t : rec.tracks()) {
    if (!t.visible || t.points.size() < 2) continue;
    QColor c = pal.warning;
    c.setAlpha(170);
    QPen pen(c, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.drawPolyline(polyline(t));
  }
  // Active track: a fading "breadcrumb" trail behind the boat.
  const Route& a = rec.activeTrack();
  if (a.points.size() >= 2) {
    const QPolygonF pts = polyline(a);
    const int n = int(pts.size());
    for (int i = 1; i < n; ++i) {
      QColor c = pal.accentBright;
      c.setAlphaF(0.25 + 0.6 * double(i) / n);
      p.setPen(QPen(c, 3, Qt::SolidLine, Qt::RoundCap));
      p.drawLine(pts[i - 1], pts[i]);
    }
    // Connect the last point with the boat.
    const auto& own = NavStore::instance().ownShip();
    if (own.valid) {
      QColor c = pal.accentBright;
      c.setAlphaF(0.85);
      p.setPen(QPen(c, 3, Qt::SolidLine, Qt::RoundCap));
      p.drawLine(pts.last(), view.screenPos(own.lat, own.lon));
    }
  }
}

TracksPage::TracksPage(ChartView* chart, QWidget* parent)
    : ListPage(parent), m_chart(chart) {
  m_record = new QPushButton;
  m_record->setCursor(Qt::PointingHandCursor);
  connect(m_record, &QPushButton::clicked, this, [] {
    auto& rec = TrackRecorder::instance();
    auto& s = AppSettings::instance();
    if (rec.isRecording())
      rec.stop();
    else
      rec.start();
    s.trackRecording = rec.isRecording();
    s.save();
  });
  actions()->addWidget(m_record);
  m_stats = new QLabel;
  m_stats->setProperty("role", "dim");
  actions()->addWidget(m_stats);
  auto* io = new QHBoxLayout;
  auto* imp = new QPushButton(tr("Import GPX"));
  auto* exp = new QPushButton(tr("Export GPX"));
  imp->setIcon(Icons::icon("import", Theme::p().text, 18));
  exp->setIcon(Icons::icon("export", Theme::p().text, 18));
  io->addWidget(imp);
  io->addWidget(exp);
  actions()->addLayout(io);
  connect(imp, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import track"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        tr("GPX files (*.gpx)"));
    if (path.isEmpty()) return;
    GpxData d;
    QString err;
    if (!GpxIO::read(path, d, &err)) {
      QMessageBox::warning(this, tr("Import failed"), err);
      return;
    }
    for (const auto& t : d.tracks) TrackRecorder::instance().addTrack(t);
  });
  connect(exp, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export tracks"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) +
            "/tracks.gpx",
        tr("GPX files (*.gpx)"));
    if (path.isEmpty()) return;
    GpxData d;
    d.tracks = TrackRecorder::instance().tracks();
    GpxIO::write(path, d);
  });
  setEmptyText(
      tr("No saved tracks. Recorded tracks appear here when "
         "recording is stopped."));

  auto& rec = TrackRecorder::instance();
  connect(&rec, &TrackRecorder::tracksChanged, this, &TracksPage::rebuild);
  connect(&rec, &TrackRecorder::recordingChanged, this,
          &TracksPage::updateRecordButton);
  connect(&rec, &TrackRecorder::activeTrackChanged, this,
          &TracksPage::updateRecordButton);
  connect(&Theme::instance(), &Theme::changed, this, [this] {
    updateRecordButton();
    rebuild();
  });
  updateRecordButton();
  rebuild();
}

void TracksPage::updateRecordButton() {
  auto& rec = TrackRecorder::instance();
  const bool on = rec.isRecording();
  m_record->setText(on ? tr("Stop recording") : tr("Start recording"));
  m_record->setProperty("role", on ? "danger" : "primary");
  m_record->setIcon(Icons::icon(on ? "stop" : "navigate", Qt::white, 18));
  m_record->style()->unpolish(m_record);
  m_record->style()->polish(m_record);
  if (on) {
    const Route& t = rec.activeTrack();
    m_stats->setText(
        tr("Recording · %1 · %2 · %n point(s)", nullptr, int(t.points.size()))
            .arg(AppSettings::instance().distanceText(t.lengthNm()),
                 durationText(TrackRecorder::durationSecs(t))));
  } else {
    m_stats->setText(tr("Track recording is off"));
  }
}

void TracksPage::rebuild() {
  clearRows();
  const Palette& pal = Theme::p();
  const auto& tracks = TrackRecorder::instance().tracks();
  for (auto it = tracks.crbegin(); it != tracks.crend(); ++it) {
    const Route& t = *it;
    auto* row = new ListRow;
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 10, 10, 10);
    auto* icon = new QLabel;
    icon->setPixmap(Icons::pixmap("route", pal.warning, 22));
    l->addWidget(icon);
    auto* col = new QVBoxLayout;
    col->setSpacing(2);
    auto* name = new QLabel(t.name);
    name->setProperty("role", "value");
    auto* sub =
        new QLabel(AppSettings::instance().distanceText(t.lengthNm()) + " · " +
                   durationText(TrackRecorder::durationSecs(t)));
    sub->setProperty("role", "dim");
    col->addWidget(name);
    col->addWidget(sub);
    l->addLayout(col, 1);
    const QString id = t.id;
    auto* vis =
        new IconButton(t.visible ? "eye" : "eye-off", tr("Show on chart"));
    connect(vis, &QAbstractButton::clicked, this, [id] {
      const Route* tr = TrackRecorder::instance().track(id);
      if (tr) TrackRecorder::instance().setTrackVisible(id, !tr->visible);
    });
    auto* del = new IconButton("trash", tr("Delete"), nullptr, true);
    connect(del, &QAbstractButton::clicked, this, [this, id] {
      if (QMessageBox::question(this, tr("Delete track"),
                                tr("Delete this track?")) == QMessageBox::Yes)
        TrackRecorder::instance().removeTrack(id);
    });
    l->addWidget(vis);
    l->addWidget(del);
    connect(row, &ListRow::clicked, this, [this, id] {
      const Route* tr = TrackRecorder::instance().track(id);
      if (!tr || tr->points.isEmpty()) return;
      double a = 90, b = 180, c = -90, d = -180;
      for (const auto& p : tr->points) {
        a = std::min(a, p.lat);
        b = std::min(b, p.lon);
        c = std::max(c, p.lat);
        d = std::max(d, p.lon);
      }
      m_chart->fitGeoBounds(a, b, c, d);
    });
    addRow(row);
  }
}
