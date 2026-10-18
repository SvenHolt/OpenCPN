// Media plugin: a "now playing" card in the style of automotive head units
// and a drawer page to pick and connect sources (Spotify, YouTube Music).
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>

#include "MediaController.h"
#include "SpotifyWebSource.h"
#include "api/OcpnPlugin.h"
#include "ui/DrawerPages.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {

QColor serviceColor(const QString& service) {
  if (service.startsWith("Spotify")) return QColor("#1DB954");
  if (service.startsWith("YouTube")) return QColor("#FF0033");
  return Theme::p().accent;
}

QString timeText(qint64 ms) {
  const qint64 s = ms / 1000;
  return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

/** Now playing card. Floating (glass) on the map or embedded in a page. */
class MediaCard : public GlassPanel {
public:
  MediaCard(MediaController* ctl, bool embedded, QWidget* parent = nullptr)
      : GlassPanel(parent, 22), m_ctl(ctl), m_embedded(embedded) {
    m_prev = new IconButton("prev", tr("Previous"), this);
    m_next = new IconButton("next", tr("Next"), this);
    m_play = new RoundButton("play", this, embedded ? 52 : 46);
    m_play->setCheckable(true);
    connect(m_prev, &QAbstractButton::clicked, this, [this] {
      if (auto* s = m_ctl->active()) s->previous();
    });
    connect(m_next, &QAbstractButton::clicked, this, [this] {
      if (auto* s = m_ctl->active()) s->next();
    });
    connect(m_play, &QAbstractButton::clicked, this, [this] {
      if (auto* s = m_ctl->active()) s->playPause();
      refresh();
    });
    connect(ctl, &MediaController::changed, this, &MediaCard::refresh);
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    m_tick.start();
    refresh();
  }

  QSize sizeHint() const override {
    return m_embedded ? QSize(380, 132)
                      : QSize(390 + 2 * kShadow, 96 + 2 * kShadow);
  }

  void setShowOnMap(bool on) {
    m_showOnMap = on;
    refresh();
  }

protected:
  QRectF content() const {
    return m_embedded ? QRectF(rect()).adjusted(0, 0, -8, 0) : panelRect();
  }

  void refresh() {
    MediaSource* s = m_ctl->active();
    const bool playing = s && s->state().playing;
    m_play->setChecked(playing);
    m_play->setIconName(playing ? "pause" : "play");
    if (!m_embedded) setVisible(m_showOnMap && s && s->state().hasTrack());
    update();
  }

  void resizeEvent(QResizeEvent*) override {
    const QRectF r = content();
    const int cy = int(m_embedded ? r.bottom() - 36 : r.center().y() - 4);
    const int right = int(r.right()) - 12;
    m_next->move(right - m_next->width(), cy - m_next->height() / 2);
    m_play->move(m_next->x() - m_play->width() - 2, cy - m_play->height() / 2);
    m_prev->move(m_play->x() - m_prev->width() - 2, cy - m_prev->height() / 2);
  }

  void paintEvent(QPaintEvent* e) override {
    const Palette& pal = Theme::p();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = content();
    if (m_embedded) {
      p.setPen(Qt::NoPen);
      p.setBrush(pal.panelHover);
      p.drawRoundedRect(r, 18, 18);
    } else {
      p.end();
      GlassPanel::paintEvent(e);
      p.begin(this);
      p.setRenderHint(QPainter::Antialiasing);
    }
    MediaSource* s = m_ctl->active();
    const NowPlaying np = s ? s->state() : NowPlaying();
    const double art = m_embedded ? 88 : 64;
    const QRectF artRect(r.left() + 14, r.top() + (m_embedded ? 14 : 16), art,
                         art);
    QPainterPath clip;
    clip.addRoundedRect(artRect, 12, 12);
    p.save();
    p.setClipPath(clip);
    const QImage img = m_ctl->artwork();
    if (!img.isNull() && np.hasTrack()) {
      p.drawImage(artRect, img);
    } else {
      QLinearGradient g(artRect.topLeft(), artRect.bottomRight());
      const QColor c = s ? serviceColor(s->service()) : pal.accent;
      g.setColorAt(0, c.lighter(130));
      g.setColorAt(1, c.darker(160));
      p.fillRect(artRect, g);
      Icons::paint(&p, "music",
                   artRect.adjusted(art / 4, art / 4, -art / 4, -art / 4),
                   Qt::white);
    }
    p.restore();

    const double x = artRect.right() + 14;
    // Embedded, the controls sit in a row below the text.
    const double w =
        (m_embedded ? r.right() - 14 : double(m_prev->x()) - 8) - x;
    p.setPen(pal.text);
    p.setFont(Theme::font(15, QFont::DemiBold));
    const QString title = np.hasTrack() ? np.title : tr("Nothing playing");
    p.drawText(QRectF(x, artRect.top() + 2, w, 20),
               Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(title, Qt::ElideRight, int(w)));
    p.setFont(Theme::font(12, QFont::Medium));
    p.setPen(pal.textDim);
    const QString artist =
        np.hasTrack() ? np.artist
                      : (s ? tr("Open %1 to start music").arg(s->service())
                           : tr("Start Spotify or YouTube Music"));
    p.drawText(QRectF(x, artRect.top() + 22, w, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(artist, Qt::ElideRight, int(w)));
    if (s) {
      const QColor sc = serviceColor(s->service());
      p.setPen(Qt::NoPen);
      p.setBrush(sc);
      p.drawEllipse(QPointF(x + 4, artRect.top() + 49), 4, 4);
      p.setPen(pal.textFaint);
      p.setFont(Theme::font(10, QFont::Bold));
      p.drawText(QRectF(x + 13, artRect.top() + 41, w, 16),
                 Qt::AlignLeft | Qt::AlignVCenter, s->service().toUpper());
    }
    // Progress
    if (np.durationMs > 0) {
      const double frac =
          qBound(0.0, double(np.positionMs) / double(np.durationMs), 1.0);
      const QRectF bar(r.left() + 14, r.bottom() - 10, r.width() - 28, 3);
      p.setPen(Qt::NoPen);
      p.setBrush(pal.panelPressed);
      p.drawRoundedRect(bar, 1.5, 1.5);
      p.setBrush(pal.text);
      p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * frac, 3),
                        1.5, 1.5);
      if (m_embedded) {
        p.setFont(Theme::numberFont(10, QFont::DemiBold));
        p.setPen(pal.textFaint);
        p.drawText(QRectF(bar.left(), bar.top() - 16, 60, 12), Qt::AlignLeft,
                   timeText(np.positionMs));
        p.drawText(QRectF(bar.right() - 60, bar.top() - 16, 60, 12),
                   Qt::AlignRight, timeText(np.durationMs));
      }
    }
  }

private:
  MediaController* m_ctl;
  bool m_embedded;
  bool m_showOnMap = true;
  IconButton* m_prev;
  IconButton* m_next;
  RoundButton* m_play;
  QTimer m_tick;
};

class MediaPage : public ListPage {
public:
  MediaPage(MediaController* ctl, MediaCard* overlay)
      : m_ctl(ctl), m_overlay(overlay) {
    auto* card = new MediaCard(ctl, true);
    card->setMinimumHeight(132);
    actions()->addWidget(card);

    auto* mapRow = new QHBoxLayout;
    auto* mapLabel = new QLabel(tr("Show player on the chart"));
    mapLabel->setProperty("role", "value");
    mapRow->addWidget(mapLabel, 1);
    auto* mapToggle = new ToggleSwitch;
    mapToggle->setChecked(QSettings().value("Media/showOnMap", true).toBool());
    overlay->setShowOnMap(mapToggle->isChecked());
    connect(mapToggle, &QAbstractButton::toggled, this, [this](bool on) {
      QSettings().setValue("Media/showOnMap", on);
      m_overlay->setShowOnMap(on);
    });
    mapRow->addWidget(mapToggle);
    actions()->addLayout(mapRow);

    // Spotify Web API
    auto* sp = new QLabel(tr("SPOTIFY ACCOUNT"));
    sp->setProperty("role", "section");
    actions()->addWidget(sp);
    m_spotifyInfo = new QLabel;
    m_spotifyInfo->setProperty("role", "dim");
    m_spotifyInfo->setWordWrap(true);
    m_spotifyInfo->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                           Qt::LinksAccessibleByMouse);
    m_spotifyInfo->setOpenExternalLinks(true);
    actions()->addWidget(m_spotifyInfo);
    m_clientId = new QLineEdit(ctl->spotifyWeb()->clientId());
    m_clientId->setPlaceholderText(tr("Spotify client ID"));
    actions()->addWidget(m_clientId);
    m_connect = new QPushButton;
    m_connect->setCursor(Qt::PointingHandCursor);
    actions()->addWidget(m_connect);
    connect(m_connect, &QPushButton::clicked, this, [this] {
      auto* web = m_ctl->spotifyWeb();
      if (web->isConnected()) {
        web->disconnectAccount();
      } else {
        web->setClientId(m_clientId->text());
        web->connectAccount();
      }
    });

    auto* yt = new QLabel(tr("YOUTUBE MUSIC"));
    yt->setProperty("role", "section");
    actions()->addWidget(yt);
    auto* ytInfo = new QLabel(
        tr("YouTube Music has no public playback API. On Linux the YouTube "
           "Music desktop app and music.youtube.com in Chrome, Firefox or "
           "Edge are detected automatically through the system media "
           "controls (MPRIS). The Spotify desktop app is detected the same "
           "way, without an account connection."));
    ytInfo->setProperty("role", "dim");
    ytInfo->setWordWrap(true);
    actions()->addWidget(ytInfo);

    auto* src = new QLabel(tr("SOURCES"));
    src->setProperty("role", "section");
    actions()->addWidget(src);
    setEmptyText(ctl->mprisAvailable()
                     ? tr("No player found. Start Spotify, YouTube Music or "
                          "play music in a browser.")
                     : tr("No player found. Connect your Spotify account "
                          "above."));
    connect(ctl, &MediaController::changed, this, &MediaPage::rebuild);
    connect(&Theme::instance(), &Theme::changed, this, &MediaPage::rebuild);
    rebuild();
  }

private:
  void rebuild() {
    auto* web = m_ctl->spotifyWeb();
    m_connect->setText(web->isConnected() ? tr("Disconnect Spotify")
                                          : tr("Connect Spotify"));
    m_connect->setProperty("role", web->isConnected() ? "ghost" : "primary");
    m_connect->style()->unpolish(m_connect);
    m_connect->style()->polish(m_connect);
    m_clientId->setVisible(!web->isConnected());
    m_spotifyInfo->setText(
        web->isConnected()
            ? tr("Connected. Spotify Connect devices (phone, speakers, "
                 "car) can be controlled. Playback control needs Premium.")
            : tr("Create an app at <a style='color:%2' "
                 "href='https://developer.spotify.com/dashboard'>"
                 "developer.spotify.com</a>, add the redirect URI <b>%1</b> "
                 "and paste its client ID here.")
                  .arg(SpotifyWebSource::redirectUri(),
                       Theme::p().accentBright.name()));

    clearRows();
    const Palette& pal = Theme::p();
    MediaSource* active = m_ctl->active();
    auto addSourceRow = [&](const QString& id, const QString& title,
                            const QString& sub, const QColor& dot,
                            bool selected) {
      auto* row = new ListRow;
      if (selected) row->setHighlight(pal.accentBright);
      auto* l = new QHBoxLayout(row);
      l->setContentsMargins(16, 8, 14, 8);
      auto* d = new QLabel;
      QPixmap pm(12, 12);
      pm.fill(Qt::transparent);
      {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(dot);
        p.drawEllipse(1, 1, 10, 10);
      }
      d->setPixmap(pm);
      l->addWidget(d);
      auto* col = new QVBoxLayout;
      col->setSpacing(2);
      auto* t = new QLabel(title);
      t->setProperty("role", "value");
      auto* s = new QLabel(sub);
      s->setProperty("role", "dim");
      col->addWidget(t);
      col->addWidget(s);
      l->addLayout(col, 1);
      connect(row, &ListRow::clicked, this,
              [this, id] { m_ctl->setPreferred(id); });
      addRow(row);
    };
    const auto sources = m_ctl->sources();
    if (!sources.isEmpty())
      addSourceRow(QString(), tr("Automatic"), tr("Follow whatever is playing"),
                   pal.textFaint, m_ctl->preferred().isEmpty());
    for (MediaSource* s : sources) {
      const NowPlaying np = s->state();
      addSourceRow(s->id(), s->service(),
                   np.hasTrack()
                       ? (np.playing ? tr("Playing: %1") : tr("Paused: %1"))
                             .arg(np.title)
                       : tr("Idle"),
                   serviceColor(s->service()),
                   !m_ctl->preferred().isEmpty() && s == active);
    }
  }

  MediaController* m_ctl;
  MediaCard* m_overlay;
  QLabel* m_spotifyInfo;
  QLineEdit* m_clientId;
  QPushButton* m_connect;
};

}  // namespace

class MediaPlugin : public QObject, public OcpnPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID OcpnPlugin_iid FILE "media.json")
  Q_INTERFACES(OcpnPlugin)

public:
  QString id() const override { return QStringLiteral("media"); }
  QString name() const override { return tr("Media"); }
  QString description() const override {
    return tr("Spotify and YouTube Music player card with playback controls.");
  }
  QString version() const override { return QStringLiteral("1.0.0"); }

  void init(PluginHost* host) override {
    m_controller = new MediaController(host->mainWindow());
    auto* card = new MediaCard(m_controller, false);
    host->addOverlay(card, Qt::BottomLeftCorner);
    host->addSection("music", tr("Media"), new MediaPage(m_controller, card));
    connect(m_controller, &MediaController::message, this,
            [host](const QString& text) { host->showMessage(text, false); });
  }

  void shutdown() override {}

private:
  MediaController* m_controller = nullptr;
};

#include "MediaPlugin.moc"
