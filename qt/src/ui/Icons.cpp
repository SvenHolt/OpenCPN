#include "ui/Icons.h"

#include <QHash>
#include <QPainter>
#include <QSvgRenderer>
#include <cmath>

namespace {
const QHash<QString, QString>& bodies() {
  static const QHash<QString, QString> b = {
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
      {"layers", R"(<path d="M12 3l9 5-9 5-9-5z"/><path d="M3 13l9 5 9-5"/>)"},
      {"search", R"(<circle cx="11" cy="11" r="6.5"/><path d="M16 16l5 5"/>)"},
      {"bell",
       R"(<path d="M6 16v-5a6 6 0 0 1 12 0v5l2 2H4z"/><path d="M10 21h4"/>)"},
      {"fullscreen", R"(<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>)"},
      {"compass",
       R"(<circle cx="12" cy="12" r="9"/><path d="M15.5 8.5l-2 5-5 2 2-5z"/>)"},
      {"chevron-right", R"(<path d="M9 5l7 7-7 7"/>)"},
      {"edit",
       R"(<path d="M4 20h4L19 9l-4-4L4 16z"/><path d="M13.5 6.5l4 4"/>)"},
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
