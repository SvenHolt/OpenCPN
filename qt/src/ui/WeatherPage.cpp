#include "ui/WeatherPage.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <cmath>

#include "core/AppSettings.h"
#include "core/GeoMath.h"
#include "core/Grib.h"
#include "core/NavStore.h"
#include "ui/ChartView.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

QColor windColor(double kn, int alpha) {
  struct Stop {
    double kn;
    QColor c;
  };
  static const Stop stops[] = {{0, QColor("#3d6fd8")},  {8, QColor("#2fb3d1")},
                               {14, QColor("#37c47a")}, {20, QColor("#e3c94a")},
                               {27, QColor("#f08a3c")}, {35, QColor("#e5484d")},
                               {50, QColor("#b23cc9")}};
  QColor c = stops[0].c;
  for (size_t i = 1; i < std::size(stops); ++i) {
    if (kn <= stops[i].kn) {
      const double t = (kn - stops[i - 1].kn) / (stops[i].kn - stops[i - 1].kn);
      const QColor& a = stops[i - 1].c;
      const QColor& b = stops[i].c;
      c = QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                           a.greenF() + (b.greenF() - a.greenF()) * t,
                           a.blueF() + (b.blueF() - a.blueF()) * t);
      break;
    }
    c = stops[i].c;
  }
  c.setAlpha(alpha);
  return c;
}

namespace {
/** Samples a field on a regular screen grid. */
struct ScreenGrid {
  int cols = 0, rows = 0;
  double spacing = 1;
  QVector<double> v;
  double at(int c, int r) const { return v[r * cols + c]; }
};

ScreenGrid sampleGrid(const ChartView& view, double spacing,
                      const std::function<double(double, double)>& f) {
  ScreenGrid g;
  g.spacing = spacing;
  g.cols = int(view.width() / spacing) + 2;
  g.rows = int(view.height() / spacing) + 2;
  g.v.resize(g.cols * g.rows);
  const QTransform inv = view.worldTransform().inverted();
  for (int r = 0; r < g.rows; ++r)
    for (int c = 0; c < g.cols; ++c) {
      QPointF w = inv.map(QPointF(c * spacing, r * spacing));
      w.setX(w.x() - std::floor(w.x()));
      double lat, lon;
      geo::fromMercator(QPointF(w.x(), qBound(0.0, w.y(), 1.0)), lat, lon);
      g.v[r * g.cols + c] = f(lat, lon);
    }
  return g;
}

QString stepLabel(const QDateTime& t) {
  const QDateTime local = t.toLocalTime();
  const qint64 h =
      std::llround(QDateTime::currentDateTimeUtc().secsTo(t) / 3600.0);
  return QLocale().toString(local, "ddd HH:mm") +
         (h == 0 ? QString()
                 : QStringLiteral("  %1%2 h").arg(h > 0 ? "+" : "").arg(h));
}
}  // namespace

// --------------------------------------------------------------------------

GribLayer::GribLayer(QObject* parent) : MapLayer(parent) {
  auto& m = GribManager::instance();
  connect(&m, &GribManager::fileChanged, this, &MapLayer::changed);
  connect(&m, &GribManager::stepChanged, this, &MapLayer::changed);
  connect(&m, &GribManager::optionsChanged, this, &MapLayer::changed);
}

void GribLayer::paint(QPainter& p, const ChartView& view) {
  auto& mgr = GribManager::instance();
  const GribTimeStep* step = mgr.currentStep();
  if (!step) return;
  const Palette& pal = Theme::p();
  p.setRenderHint(QPainter::Antialiasing);

  const bool hasWind = step->windU && step->windV;
  auto windAt = [&](double lat, double lon, double& kn, double& to) {
    const double u = step->windU->sample(lat, lon);
    const double v = step->windV->sample(lat, lon);
    if (std::isnan(u) || std::isnan(v)) return false;
    kn = std::hypot(u, v) * 1.943844;
    to = std::atan2(u, v) * 180 / M_PI;  // direction the wind blows to
    return true;
  };

  // 1. Wind speed colors, sampled coarsely and smoothly scaled up.
  if (hasWind && mgr.showHeatmap) {
    const double s = 10;
    const ScreenGrid g = sampleGrid(view, s, [&](double lat, double lon) {
      double kn, to;
      return windAt(lat, lon, kn, to) ? kn : qQNaN();
    });
    QImage img(g.cols, g.rows, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    const int alpha = pal.dark ? 95 : 110;
    for (int r = 0; r < g.rows; ++r)
      for (int c = 0; c < g.cols; ++c) {
        const double kn = g.at(c, r);
        if (std::isnan(kn)) continue;
        const QColor col = windColor(kn, alpha);
        img.setPixel(c, r, qPremultiply(col.rgba()));
      }
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(-s / 2, -s / 2, g.cols * s, g.rows * s), img);
    p.restore();
  }

  // 2. Isobars every 4 hPa (marching squares on a screen grid).
  if (step->pressure && mgr.showPressure) {
    const double s = 18;
    const ScreenGrid g = sampleGrid(view, s, [&](double lat, double lon) {
      return step->pressure->sample(lat, lon) / 100.0;  // hPa
    });
    double lo = 1e9, hi = -1e9;
    for (double v : g.v)
      if (!std::isnan(v)) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
    QColor line = pal.dark ? QColor(255, 255, 255, 120) : QColor(0, 0, 0, 110);
    p.setFont(Theme::font(10, QFont::DemiBold));
    for (double level = std::ceil(lo / 4) * 4; level <= hi; level += 4) {
      QPainterPath path;
      QPointF labelPos;
      for (int r = 0; r + 1 < g.rows; ++r)
        for (int c = 0; c + 1 < g.cols; ++c) {
          const double v[4] = {g.at(c, r), g.at(c + 1, r), g.at(c + 1, r + 1),
                               g.at(c, r + 1)};
          if (std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2]) ||
              std::isnan(v[3]))
            continue;
          const QPointF corner[4] = {
              QPointF(c * s, r * s), QPointF((c + 1) * s, r * s),
              QPointF((c + 1) * s, (r + 1) * s), QPointF(c * s, (r + 1) * s)};
          QPointF hits[4];
          int n = 0;
          for (int e = 0; e < 4; ++e) {
            const double a = v[e], b = v[(e + 1) % 4];
            if ((a < level) != (b < level)) {
              const double t = (level - a) / (b - a);
              hits[n++] = corner[e] + (corner[(e + 1) % 4] - corner[e]) * t;
            }
          }
          for (int k = 0; k + 1 < n; k += 2) {
            path.moveTo(hits[k]);
            path.lineTo(hits[k + 1]);
            if (hits[k].x() > labelPos.x()) labelPos = hits[k];
          }
        }
      if (path.isEmpty()) continue;
      const bool major = std::fmod(level, 20) == 0;
      p.setPen(QPen(line, major ? 1.8 : 1.1));
      p.setBrush(Qt::NoBrush);
      p.drawPath(path);
      // Label near the right edge of the isobar.
      const QString text = QString::number(int(level));
      const QRectF lr(std::min(labelPos.x(), view.width() - 140.0) - 18,
                      labelPos.y() - 9, 36, 18);
      p.setPen(Qt::NoPen);
      p.setBrush(pal.panel);
      p.drawRoundedRect(lr, 9, 9);
      p.setPen(pal.text);
      p.drawText(lr, Qt::AlignCenter, text);
    }
  }

  // 3. Wind arrows
  if (hasWind && mgr.showWind) {
    const double s = 64;
    const QTransform inv = view.worldTransform().inverted();
    const bool labels = true;
    p.setFont(Theme::font(10, QFont::Bold));
    for (double y = s / 2; y < view.height(); y += s)
      for (double x = s / 2; x < view.width(); x += s) {
        QPointF w = inv.map(QPointF(x, y));
        w.setX(w.x() - std::floor(w.x()));
        double lat, lon;
        geo::fromMercator(QPointF(w.x(), qBound(0.0, w.y(), 1.0)), lat, lon);
        double kn, to;
        if (!windAt(lat, lon, kn, to)) continue;
        const double len = 10 + std::min(kn, 40.0) * 0.45;
        p.save();
        p.translate(x, y);
        p.rotate(to - view.rotation());
        QPainterPath arrow;
        arrow.moveTo(0, -len / 2 - 6);
        arrow.lineTo(5.5, -len / 2 + 2);
        arrow.lineTo(1.6, -len / 2 + 1);
        arrow.lineTo(1.6, len / 2);
        arrow.lineTo(-1.6, len / 2);
        arrow.lineTo(-1.6, -len / 2 + 1);
        arrow.lineTo(-5.5, -len / 2 + 2);
        arrow.closeSubpath();
        p.setPen(
            QPen(pal.dark ? QColor(0, 0, 0, 140) : QColor(255, 255, 255), 1.2));
        p.setBrush(windColor(kn).lighter(pal.dark ? 125 : 100));
        p.drawPath(arrow);
        p.restore();
        if (labels) {
          p.setPen(pal.text);
          p.drawText(QRectF(x - 20, y + len / 2 + 2, 40, 14), Qt::AlignCenter,
                     QString::number(int(std::lround(kn))));
        }
      }
  }
}

// --------------------------------------------------------------------------

WeatherTimeBar::WeatherTimeBar(QWidget* parent) : GlassPanel(parent, 22) {
  m_play = new RoundButton("play", this, 44);
  m_play->setCheckable(true);
  m_play->setToolTip(tr("Animate forecast"));
  m_timer.setInterval(700);
  connect(&m_timer, &QTimer::timeout, this, [] {
    auto& m = GribManager::instance();
    if (!m.file()) return;
    const int n = int(m.file()->steps().size());
    m.setStep((m.step() + 1) % std::max(1, n));
  });
  connect(m_play, &QAbstractButton::toggled, this, [this](bool on) {
    m_play->setIconName(on ? "pause" : "play");
    on ? m_timer.start() : m_timer.stop();
  });
  auto& m = GribManager::instance();
  connect(&m, &GribManager::stepChanged, this, qOverload<>(&QWidget::update));
  connect(&m, &GribManager::fileChanged, this, [this] {
    const bool has = bool(GribManager::instance().file());
    setVisible(has);
    if (!has) m_play->setChecked(false);
  });
  setCursor(Qt::PointingHandCursor);
  hide();
}

void WeatherTimeBar::resizeEvent(QResizeEvent*) {
  const QRectF r = panelRect();
  m_play->move(int(r.left() + 10), int(r.center().y() - m_play->height() / 2));
}

QRectF WeatherTimeBar::trackRect() const {
  const QRectF r = panelRect();
  return QRectF(r.left() + 66, r.center().y() + 8, r.width() - 86, 6);
}

void WeatherTimeBar::paintEvent(QPaintEvent* e) {
  GlassPanel::paintEvent(e);
  auto& m = GribManager::instance();
  if (!m.file() || m.file()->steps().isEmpty()) return;
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF track = trackRect();
  const int n = int(m.file()->steps().size());
  const double t = n > 1 ? double(m.step()) / (n - 1) : 0;
  p.setFont(Theme::font(13, QFont::DemiBold));
  p.setPen(pal.text);
  p.drawText(QRectF(track.left(), track.top() - 30, track.width(), 20),
             Qt::AlignLeft | Qt::AlignVCenter,
             stepLabel(m.file()->steps()[m.step()].time));
  p.setFont(Theme::font(11, QFont::DemiBold));
  p.setPen(pal.textDim);
  p.drawText(QRectF(track.left(), track.top() - 30, track.width(), 20),
             Qt::AlignRight | Qt::AlignVCenter, tr("WIND · GFS"));
  p.setPen(Qt::NoPen);
  p.setBrush(pal.panelPressed);
  p.drawRoundedRect(track, 3, 3);
  p.setBrush(pal.accentBright);
  p.drawRoundedRect(
      QRectF(track.left(), track.top(), track.width() * t, track.height()), 3,
      3);
  p.setBrush(Qt::white);
  p.drawEllipse(QPointF(track.left() + track.width() * t, track.center().y()),
                7, 7);
}

void WeatherTimeBar::seek(const QPointF& pos) {
  auto& m = GribManager::instance();
  if (!m.file()) return;
  const QRectF track = trackRect();
  const int n = int(m.file()->steps().size());
  const double t = qBound(0.0, (pos.x() - track.left()) / track.width(), 1.0);
  m.setStep(int(std::lround(t * (n - 1))));
}

void WeatherTimeBar::mousePressEvent(QMouseEvent* e) { seek(e->position()); }
void WeatherTimeBar::mouseMoveEvent(QMouseEvent* e) { seek(e->position()); }

// --------------------------------------------------------------------------

WeatherPage::WeatherPage(ChartView* chart, QWidget* parent)
    : QWidget(parent), m_chart(chart) {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 8, 0);
  l->setSpacing(10);

  m_downloadButton = new QPushButton(tr("Download wind forecast"));
  m_downloadButton->setProperty("role", "primary");
  m_downloadButton->setIcon(Icons::icon("import", Qt::white, 18));
  m_downloadButton->setCursor(Qt::PointingHandCursor);
  connect(m_downloadButton, &QPushButton::clicked, this,
          &WeatherPage::download);
  l->addWidget(m_downloadButton);
  auto* open = new QPushButton(tr("Open GRIB file"));
  open->setIcon(Icons::icon("layers", Theme::p().text, 18));
  open->setCursor(Qt::PointingHandCursor);
  connect(open, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open GRIB file"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        tr("GRIB files (*.grb *.grb2 *.grib *.grib2 *.bz2);;All files (*)"));
    if (path.isEmpty()) return;
    QString err;
    if (!GribManager::instance().open(path, &err))
      QMessageBox::warning(this, tr("GRIB"), err);
  });
  l->addWidget(open);

  m_progress = new QProgressBar;
  m_progress->setTextVisible(false);
  m_progress->setFixedHeight(6);
  m_progress->setStyleSheet(
      QStringLiteral("QProgressBar { background: %1; border: none; "
                     "border-radius: 3px; } QProgressBar::chunk { "
                     "background: %2; border-radius: 3px; }")
          .arg(Theme::p().panelPressed.name(QColor::HexArgb),
               Theme::p().accent.name()));
  m_progress->hide();
  l->addWidget(m_progress);
  m_status = new QLabel;
  m_status->setProperty("role", "dim");
  m_status->setWordWrap(true);
  l->addWidget(m_status);

  auto* section = new QLabel(tr("FORECAST"));
  section->setProperty("role", "section");
  l->addWidget(section);
  m_info = new QLabel;
  m_info->setWordWrap(true);
  m_info->setProperty("role", "dim");
  l->addWidget(m_info);
  m_readout = new QLabel;
  m_readout->setProperty("role", "value");
  m_readout->setWordWrap(true);
  l->addWidget(m_readout);

  m_layers = new QWidget;
  auto* ll = new QVBoxLayout(m_layers);
  ll->setContentsMargins(0, 0, 0, 0);
  auto* layersTitle = new QLabel(tr("LAYERS"));
  layersTitle->setProperty("role", "section");
  ll->addWidget(layersTitle);
  auto addToggle = [&](const QString& text, bool on, auto apply) {
    auto* row = new QHBoxLayout;
    auto* lab = new QLabel(text);
    lab->setProperty("role", "value");
    row->addWidget(lab, 1);
    auto* t = new ToggleSwitch;
    t->setChecked(on);
    connect(t, &QAbstractButton::toggled, this, apply);
    row->addWidget(t);
    ll->addLayout(row);
  };
  auto& mgr = GribManager::instance();
  addToggle(tr("Wind arrows"), mgr.showWind, [](bool v) {
    auto& m = GribManager::instance();
    m.setOptions(v, m.showPressure, m.showHeatmap);
  });
  addToggle(tr("Wind speed colors"), mgr.showHeatmap, [](bool v) {
    auto& m = GribManager::instance();
    m.setOptions(m.showWind, m.showPressure, v);
  });
  addToggle(tr("Isobars"), mgr.showPressure, [](bool v) {
    auto& m = GribManager::instance();
    m.setOptions(m.showWind, v, m.showHeatmap);
  });
  // Legend
  auto* legend = new QLabel;
  QPixmap pm(360, 34);
  pm.fill(Qt::transparent);
  {
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    for (int x = 0; x < 360; ++x) {
      p.setPen(windColor(x / 360.0 * 50));
      p.drawLine(x, 0, x, 10);
    }
    p.setPen(Theme::p().textDim);
    p.setFont(Theme::font(10, QFont::DemiBold));
    for (int kn : {0, 10, 20, 30, 40, 50})
      p.drawText(QRectF(kn / 50.0 * 340, 14, 30, 16), Qt::AlignLeft,
                 QString::number(kn) + (kn == 50 ? " kn" : ""));
  }
  legend->setPixmap(pm);
  ll->addWidget(legend);
  l->addWidget(m_layers);

  m_close = new QPushButton(tr("Close forecast"));
  m_close->setProperty("role", "ghost");
  connect(m_close, &QPushButton::clicked, this,
          [] { GribManager::instance().close(); });
  l->addWidget(m_close);
  l->addStretch(1);

  connect(&m_download, &GribDownload::progress, this,
          [this](int done, int total, const QString& text) {
            m_progress->show();
            m_progress->setRange(0, total);
            m_progress->setValue(done);
            m_status->setText(text);
          });
  connect(&m_download, &GribDownload::finished, this,
          [this](const QString& path) {
            m_progress->hide();
            m_downloadButton->setEnabled(true);
            QString err;
            if (!GribManager::instance().open(path, &err))
              m_status->setText(err);
            else
              m_status->setText(tr("Forecast saved to %1").arg(path));
          });
  connect(&m_download, &GribDownload::failed, this, [this](const QString& err) {
    m_progress->hide();
    m_downloadButton->setEnabled(true);
    m_status->setText(tr("Download failed: %1").arg(err));
  });
  connect(&mgr, &GribManager::fileChanged, this, &WeatherPage::refresh);
  connect(&mgr, &GribManager::stepChanged, this, &WeatherPage::refresh);
  connect(&NavStore::instance(), &NavStore::ownShipChanged, this, [this] {
    if (isVisible()) refresh();
  });
  refresh();
}

void WeatherPage::download() {
  // Visible chart area plus a margin, at most 30 x 30 degrees.
  double lat1, lon1, lat2, lon2;
  m_chart->geoPos(QPointF(0, m_chart->height()), lat1, lon1);
  m_chart->geoPos(QPointF(m_chart->width(), 0), lat2, lon2);
  double clat, clon;
  m_chart->center(clat, clon);
  const double hlat = std::clamp((lat2 - lat1) * 0.75, 2.0, 15.0);
  double dlon = lon2 - lon1;
  if (dlon < 0) dlon += 360;
  const double hlon = std::clamp(dlon * 0.75, 3.0, 15.0);
  m_downloadButton->setEnabled(false);
  m_status->setText(tr("Contacting NOAA NOMADS…"));
  m_download.start(clat - hlat, clon - hlon, clat + hlat, clon + hlon);
}

void WeatherPage::refresh() {
  auto& mgr = GribManager::instance();
  const auto file = mgr.file();
  m_layers->setVisible(bool(file));
  m_close->setVisible(bool(file));
  if (!file) {
    m_info->setText(
        tr("No forecast loaded. Download the NOAA GFS wind "
           "forecast for the visible area or open a GRIB file "
           "(GRIB1/GRIB2, e.g. from Saildocs or XyGrib)."));
    m_readout->clear();
    return;
  }
  const auto& steps = file->steps();
  QString info = QFileInfo(file->path()).fileName();
  if (!file->fields().isEmpty()) {
    const GribField& f = file->fields().first();
    info += "\n" + tr("Run %1 UTC · %n step(s)", nullptr, int(steps.size()))
                       .arg(f.refTime.toString("dd.MM.yyyy hh:mm"));
  }
  m_info->setText(info);
  const auto& own = NavStore::instance().ownShip();
  double lat, lon;
  if (own.valid) {
    lat = own.lat;
    lon = own.lon;
  } else {
    m_chart->center(lat, lon);
  }
  QString text;
  double kn, dir;
  if (file->wind(mgr.step(), lat, lon, kn, dir))
    text += tr("Wind %1 from %2°")
                .arg(AppSettings::instance().speedText(kn))
                .arg(int(std::lround(dir)) % 360, 3, 10, QChar('0'));
  if (const GribTimeStep* s = mgr.currentStep()) {
    if (s->gust) {
      const double g = s->gust->sample(lat, lon);
      if (!std::isnan(g))
        text +=
            "\n" +
            tr("Gusts %1").arg(AppSettings::instance().speedText(g * 1.943844));
    }
    if (s->pressure) {
      const double pr = s->pressure->sample(lat, lon);
      if (!std::isnan(pr))
        text += "\n" + tr("Pressure %1 hPa").arg(pr / 100.0, 0, 'f', 1);
    }
  }
  m_readout->setText(text.isEmpty() ? tr("Outside the forecast area")
                                    : (own.valid ? tr("At the boat:\n")
                                                 : tr("At chart center:\n")) +
                                          text);
}
