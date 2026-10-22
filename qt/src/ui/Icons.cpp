#include "ui/Icons.h"

#include <QHash>
#include <QPainter>
#include <QSvgRenderer>
#include <cmath>

namespace {
const QHash<QString, QString>& bodies() {
  static const QHash<QString, QString> b =
      {
          {"chart",
           R"(<path d="M3 6l6-3 6 3 6-3v15l-6 3-6-3-6 3z"/><path d="M9 3v15M15 6v15"/>)"},
          {"route",
           R"(<circle cx="6" cy="19" r="2.5"/><circle cx="18" cy="5" r="2.5"/><path d="M8.5 19H16a3.5 3.5 0 0 0 0-7H8a3.5 3.5 0 0 1 0-7h7.5"/>)"},
          {"pin",
           R"(<path d="M12 21s-7-6.2-7-11.5a7 7 0 0 1 14 0C19 14.8 12 21 12 21z"/><circle cx="12" cy="9.5" r="2.5"/>)"},
          {"ship",
           R"(<path d="M12 3l5 13-5-2.8L7 16z"/><path d="M3 20.5c1.5 1 3 1 4.5 0s3-1 4.5 0 3 1 4.5 0 3-1 4.5 0"/>)"},
          {"signal",
           R"(<path d="M4.9 9.5a10 10 0 0 1 14.2 0"/><path d="M8 12.6a5.6 5.6 0 0 1 8 0"/><circle cx="12" cy="16" r="1.4" fill="currentColor"/><path d="M12 17.5V21"/>)"},
          {"sliders",
           R"(<path d="M4 6h9M19 6h1M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="16" cy="6" r="2.4"/><circle cx="9" cy="12" r="2.4"/><circle cx="17" cy="18" r="2.4"/>)"},
          {"plus", R"(<path d="M12 5v14M5 12h14"/>)"},
          {"minus", R"(<path d="M5 12h14"/>)"},
          {"north",
           R"(<path d="M12 3l4.5 9h-9z" fill="currentColor"/><path d="M12 21l-4.5-9h9z"/>)"},
          {"locate",
           R"(<circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="2.5" fill="currentColor"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3"/>)"},
          {"sun",
           R"(<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>)"},
          {"moon",
           R"(<path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/>)"},
          {"lifebuoy",
           R"(<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4"/><path d="M5.6 5.6l3.6 3.6M14.8 14.8l3.6 3.6M18.4 5.6l-3.6 3.6M9.2 14.8l-3.6 3.6"/>)"},
          {"close", R"(<path d="M6 6l12 12M18 6L6 18"/>)"},
          {"undo",
           R"(<path d="M9 14L4 9l5-5"/><path d="M4 9h10.5a5.5 5.5 0 0 1 0 11H11"/>)"},
          {"check", R"(<path d="M5 12.5l4.5 4.5L19 7"/>)"},
          {"trash",
           R"(<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>)"},
          {"navigate", R"(<path d="M4 11l16-7-7 16-2-7z"/>)"},
          {"stop", R"(<rect x="6" y="6" width="12" height="12" rx="2.5"/>)"},
          {"eye",
           R"(<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>)"},
          {"eye-off",
           R"(<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><path d="M3 3l18 18"/>)"},
          {"import", R"(<path d="M12 3v12M7 10l5 5 5-5M4 21h16"/>)"},
          {"export", R"(<path d="M12 15V3M7 8l5-5 5 5M4 21h16"/>)"},
          {"arrow-up", R"(<path d="M12 20V5M6 11l6-6 6 6"/>)"},
          {"turn-left",
           R"(<path d="M16 20v-7a4 4 0 0 0-4-4H5"/><path d="M9 5L5 9l4 4"/>)"},
          {"turn-right",
           R"(<path d="M8 20v-7a4 4 0 0 1 4-4h7"/><path d="M15 5l4 4-4 4"/>)"},
          {"flag", R"(<path d="M5 21V4M5 4h11l-2 4 2 4H5"/>)"},
          {"layers",
           R"(<path d="M12 3l9 5-9 5-9-5z"/><path d="M3 13l9 5 9-5"/>)"},
          {"search",
           R"(<circle cx="11" cy="11" r="6.5"/><path d="M16 16l5 5"/>)"},
          {"bell",
           R"(<path d="M6 16v-5a6 6 0 0 1 12 0v5l2 2H4z"/><path d="M10 21h4"/>)"},
          {"fullscreen",
           R"(<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>)"},
          {"compass",
           R"(<circle cx="12" cy="12" r="9"/><path d="M15.5 8.5l-2 5-5 2 2-5z"/>)"},
          {"chevron-right", R"(<path d="M9 5l7 7-7 7"/>)"},
          {"tide",
           R"(<path d="M2 15c2.5-2.5 4.5-2.5 7 0s4.5 2.5 7 0 4.5-2.5 6 0"/><path d="M2 20c2.5-2.5 4.5-2.5 7 0s4.5 2.5 7 0 4.5-2.5 6 0"/><path d="M12 3v8M8.5 7.5L12 11l3.5-3.5"/>)"},
          {"wind",
           R"(<path d="M3 8h11a3 3 0 1 0-3-3"/><path d="M3 12h16a3 3 0 1 1-3 3"/><path d="M3 16h7"/>)"},
          {"music",
           R"(<path d="M9 18V5l11-2v13"/><circle cx="6.5" cy="18" r="2.5"/><circle cx="17.5" cy="16" r="2.5"/>)"},
          {"play", R"(<path d="M7 4.5v15l12-7.5z" fill="currentColor"/>)"},
          {"pause",
           R"(<rect x="6" y="5" width="4" height="14" rx="1" fill="currentColor"/><rect x="14" y="5" width="4" height="14" rx="1" fill="currentColor"/>)"},
          {"next",
           R"(<path d="M5 5v14l10-7z" fill="currentColor"/><path d="M18 5v14" stroke-width="2.4"/>)"},
          {"prev",
           R"(<path d="M19 5v14L9 12z" fill="currentColor"/><path d="M6 5v14" stroke-width="2.4"/>)"},
          {"sail",
           R"(<path d="M11 3v15"/><path d="M11 4c4 3 7 8 7 13h-7"/><path d="M9 6c-2.5 3-4 7-4 11h4"/><path d="M3 20.5c3 1.2 15 1.2 18 0"/>)"},
          {"engine",
           R"(<path d="M4 10h3l2-2h5l2 2h2v2h2v5h-2v2h-3l-2 2H8l-1-2H4z"/><path d="M8 5h6M11 5v3"/>)"},
          {"gauge",
           R"(<path d="M4 18a8 8 0 1 1 16 0"/><path d="M12 18l4-6"/><circle cx="12" cy="18" r="1.4" fill="currentColor"/>)"},
          {"edit",
           R"(<path d="M4 20h4L19 9l-4-4L4 16z"/><path d="M13.5 6.5l4 4"/>)"},
          {"boat",
           R"(<path d="M3 15h18l-3 4.5H6z"/><path d="M12 3v11"/><path d="M12 4.5l6.5 8.5H12"/><path d="M11 6.5L6 13h5"/>)"},
          {"more",
           R"(<circle cx="5" cy="12" r="1.6" fill="currentColor"/><circle cx="12" cy="12" r="1.6" fill="currentColor"/><circle cx="19" cy="12" r="1.6" fill="currentColor"/>)"},
          {"download",
           R"(<path d="M12 4v11"/><path d="M7 10.5l5 5 5-5"/><path d="M5 20h14"/>)"},
          {"refresh",
           R"(<path d="M19 8a8 8 0 1 0 1 6"/><path d="M20 3v5h-5"/>)"},
          {"crop", R"(<path d="M7 3v14h14"/><path d="M3 7h14v14"/>)"},
          {"anchor",
           R"(<circle cx="12" cy="5" r="2"/><path d="M12 7v14"/><path d="M8 11h8"/><path d="M4 14a8 7 0 0 0 16 0"/>)"},
          {"fuel",
           R"(<path d="M5 20V5a1 1 0 0 1 1-1h7a1 1 0 0 1 1 1v15"/><path d="M4 20h11"/><path d="M7 8h5"/><path d="M14 10h2a2 2 0 0 1 2 2v4a1.5 1.5 0 0 0 3 0V8l-3-3"/>)"},
          {"bulb",
           R"(<path d="M9 18h6M10 21h4"/><path d="M12 3a6 6 0 0 0-3.5 10.9c.6.5 1 1.2 1 2.1h5c0-.9.4-1.6 1-2.1A6 6 0 0 0 12 3z"/>)"},
          {"drop",
           R"(<path d="M12 3s6 6.6 6 11a6 6 0 0 1-12 0c0-4.4 6-11 6-11z"/>)"},
          {"auto", R"(<path d="M13 3L5 13h6l-1 8 8-10h-6z"/>)"},
          {"back", R"(<path d="M15 5l-7 7 7 7"/>)"},
          {"chevron", R"(<path d="M9 5l7 7-7 7"/>)"},
          {"heat",
           R"(<path d="M8 4c-1.5 2 1.5 3.5 0 6M12 4c-1.5 2 1.5 3.5 0 6M16 4c-1.5 2 1.5 3.5 0 6"/><rect x="4" y="13" width="16" height="7" rx="2"/>)"},
          {"palette",
           R"(<path d="M12 3a9 9 0 0 0 0 18c1.4 0 2-1 2-2s-1-1.5-1-2.5S14 15 15 15h2a4 4 0 0 0 4-4c0-4.4-4-8-9-8z"/><circle cx="7.5" cy="11" r="1.2" fill="currentColor"/><circle cx="10.5" cy="7" r="1.2" fill="currentColor"/><circle cx="15" cy="7.5" r="1.2" fill="currentColor"/>)"},
          {"basket",
           R"(<path d="M3 10h18l-2 10H5z"/><path d="M8 10l3-6M16 10l-3-6"/><path d="M9 14v3M12 14v3M15 14v3"/>)"},
          {"apple",
           R"(<path d="M12 7c-2-1.5-7-1.5-7 4 0 4 3 9 5 9 1 0 1.5-.5 2-.5s1 .5 2 .5c2 0 5-5 5-9 0-5.5-5-5.5-7-4z"/><path d="M12 7c0-2 1-3.5 3-4"/>)"},
          {"carrot",
           R"(<path d="M15 9L4 20c-.5.5 0 1 .5.5L16 10"/><path d="M15 9c1.5-1.5 4-1 4 1s-2.5 2.5-4 1z"/><path d="M17 7l3-3M15 6l1-3M18 9l3-1"/><path d="M8 14l2 2M11 11l2 2"/>)"},
          {"jar",
           R"(<rect x="7" y="3" width="10" height="3" rx="1"/><path d="M8 6c-2 1-2 2-2 4v8a3 3 0 0 0 3 3h6a3 3 0 0 0 3-3v-8c0-2 0-3-2-4"/><path d="M6 12h12"/>)"},
          {"can",
           R"(<ellipse cx="12" cy="5" rx="6" ry="2"/><path d="M6 5v14c0 1.1 2.7 2 6 2s6-.9 6-2V5"/><path d="M6 9c0 1.1 2.7 2 6 2s6-.9 6-2"/>)"},
          {"fridge",
           R"(<rect x="6" y="2.5" width="12" height="19" rx="2"/><path d="M6 9.5h12M9 5.5v2M9 12.5v3"/>)"},
          {"snow",
           R"(<path d="M12 2v20M3.3 7l17.4 10M3.3 17L20.7 7"/><path d="M9.5 3.5L12 6l2.5-2.5M9.5 20.5L12 18l2.5 2.5"/>)"},
          {"battery",
           R"(<rect x="3" y="7" width="16" height="10" rx="2"/><path d="M21 10.5v3"/><path d="M6 10v4M9 10v4M12 10v4"/>)"},
          {"list",
           R"(<path d="M9 6h11M9 12h11M9 18h11"/><circle cx="4.5" cy="6" r="1.2" fill="currentColor"/><circle cx="4.5" cy="12" r="1.2" fill="currentColor"/><circle cx="4.5" cy="18" r="1.2" fill="currentColor"/>)"},
          {"thermo",
           R"(<path d="M10 14V5a2 2 0 0 1 4 0v9a4 4 0 1 1-4 0z"/><path d="M12 9v7"/>)"},
          {"book",
           R"(<path d="M5 4.5A1.5 1.5 0 0 1 6.5 3H19v15H6.5A1.5 1.5 0 0 0 5 19.5z"/><path d="M5 19.5A1.5 1.5 0 0 0 6.5 21H19v-3"/><path d="M9 7.5h6M9 11h4"/>)"},
          {"database",
           R"(<ellipse cx="12" cy="6" rx="7" ry="3"/><path d="M5 6v12c0 1.7 3.1 3 7 3s7-1.3 7-3V6"/><path d="M5 12c0 1.7 3.1 3 7 3s7-1.3 7-3"/>)"},
      };
  return b;
}
}  // namespace

namespace Icons {

static void renderSvg(QPainter* p, const QString& name, const QRectF& rect,
                      const QColor& color) {
  QString body = bodies().value(name);
  if (body.isEmpty()) return;
  const QString c = color.name(QColor::HexRgb);
  body.replace("currentColor", c);
  const QString svg =
      QStringLiteral(
          "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' "
          "fill='none' stroke='%1' stroke-opacity='%2' fill-opacity='%2' "
          "stroke-width='1.8' stroke-linecap='round' "
          "stroke-linejoin='round'>%3</svg>")
          .arg(c)
          .arg(color.alphaF(), 0, 'f', 3)
          .arg(body);
  QSvgRenderer r(svg.toUtf8());
  r.render(p, rect);
}

QPixmap pixmap(const QString& name, const QColor& color, int size, qreal dpr) {
  static QHash<QString, QPixmap> cache;
  const QString key = QStringLiteral("%1|%2|%3|%4")
                          .arg(name)
                          .arg(color.rgba())
                          .arg(size)
                          .arg(dpr);
  auto it = cache.constFind(key);
  if (it != cache.constEnd()) return *it;
  QPixmap pm(int(size * dpr), int(size * dpr));
  pm.fill(Qt::transparent);
  {
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    renderSvg(&p, name, QRectF(0, 0, pm.width(), pm.height()), color);
  }
  pm.setDevicePixelRatio(dpr);
  if (cache.size() > 2000) cache.clear();
  cache.insert(key, pm);
  return pm;
}

void paint(QPainter* p, const QString& name, const QRectF& rect,
           const QColor& color) {
  const qreal dpr = qMax<qreal>(2.0, p->device()->devicePixelRatioF());
  const int size = int(std::ceil(qMax(rect.width(), rect.height())));
  p->drawPixmap(rect, pixmap(name, color, size, dpr),
                QRectF(0, 0, size * dpr, size * dpr));
}

QIcon icon(const QString& name, const QColor& color, int size) {
  return QIcon(pixmap(name, color, size));
}

}  // namespace Icons
