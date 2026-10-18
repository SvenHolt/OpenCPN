// Monochrome line icons rendered from inline SVG in any color.
#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

class QPainter;

namespace Icons {
QPixmap pixmap(const QString& name, const QColor& color, int size,
               qreal dpr = 2.0);
QIcon icon(const QString& name, const QColor& color, int size = 24);
void paint(QPainter* p, const QString& name, const QRectF& rect,
           const QColor& color);
}  // namespace Icons
