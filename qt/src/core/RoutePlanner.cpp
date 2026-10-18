#include "core/RoutePlanner.h"

#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QRegion>
#include <algorithm>
#include <climits>
#include <limits>
#include <cmath>
#include <queue>
#include <vector>

#include "core/GeoMath.h"

namespace {
QString tr(const char* s) {
  return QCoreApplication::translate("RoutePlanner", s);
}

struct Grid {
  int w = 0, h = 0;
  std::vector<quint8> blocked;
  bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
  bool free(int x, int y) const { return inside(x, y) && !blocked[y * w + x]; }
};

/** Labels 4-connected free cells; returns labels (-1 = blocked). */
std::vector<int> components(const Grid& g, int* count) {
  std::vector<int> label(g.blocked.size(), -1);
  std::vector<int> stack;
  int n = 0;
  for (int start = 0; start < int(label.size()); ++start) {
    if (g.blocked[start] || label[start] >= 0) continue;
    label[start] = n;
    stack.push_back(start);
    while (!stack.empty()) {
      const int c = stack.back();
      stack.pop_back();
      const int x = c % g.w, y = c / g.w;
      const int nb[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
      for (const auto& p : nb) {
        if (!g.free(p[0], p[1])) continue;
        const int i = p[1] * g.w + p[0];
        if (label[i] >= 0) continue;
        label[i] = n;
        stack.push_back(i);
      }
    }
    ++n;
  }
  if (count) *count = n;
  return label;
}

Grid dilate(const Grid& g, int r) {
  if (r <= 0) return g;
  Grid out = g;
  // Separable square kernel: rows, then columns.
  std::vector<quint8> tmp(g.blocked.size(), 0);
  for (int y = 0; y < g.h; ++y)
    for (int x = 0; x < g.w; ++x)
      if (g.blocked[y * g.w + x])
        for (int dx = -r; dx <= r; ++dx)
          if (g.inside(x + dx, y)) tmp[y * g.w + x + dx] = 1;
  for (int y = 0; y < g.h; ++y)
    for (int x = 0; x < g.w; ++x)
      if (tmp[y * g.w + x])
        for (int dy = -r; dy <= r; ++dy)
          if (g.inside(x, y + dy)) out.blocked[(y + dy) * g.w + x] = 1;
  return out;
}

/** True if the straight line between two points (grid coordinates) crosses
 *  no blocked cell. Cells closer than `ignoreRadius` to `ignoreAt` are not
 *  checked (start and destination may lie on land). The line is sampled
 *  finely enough to catch diagonal corners. */
bool lineClear(const Grid& g, QPointF a, QPointF b,
               const QVector<QPair<QPointF, double>>& ignore = {}) {
  const QPointF d = b - a;
  const int steps =
      int(std::ceil(std::max(std::fabs(d.x()), std::fabs(d.y())) * 3));
  for (int i = 0; i <= steps; ++i) {
    const QPointF p = a + d * (steps ? double(i) / steps : 0);
    bool skip = false;
    for (const auto& [c, r] : ignore) skip |= QLineF(p, c).length() < r;
    if (skip) continue;
    for (double ox : {-0.3, 0.3})
      for (double oy : {-0.3, 0.3})
        if (!g.free(int(std::floor(p.x() + ox)), int(std::floor(p.y() + oy))))
          return false;
  }
  return true;
}

/** A* on the 8-connected grid without cutting corners. */
std::vector<int> astar(const Grid& g, int start, int goal) {
  const int n = g.w * g.h;
  std::vector<float> cost(n, std::numeric_limits<float>::infinity());
  std::vector<int> from(n, -1);
  std::vector<quint8> closed(n, 0);
  using Item = std::pair<float, int>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
  const int gx = goal % g.w, gy = goal / g.w;
  const auto h = [&](int c) {
    const float dx = std::abs(c % g.w - gx), dy = std::abs(c / g.w - gy);
    return std::max(dx, dy) + (float(M_SQRT2) - 1) * std::min(dx, dy);
  };
  cost[start] = 0;
  open.push({h(start), start});
  while (!open.empty()) {
    const int c = open.top().second;
    open.pop();
    if (closed[c]) continue;
    if (c == goal) break;
    closed[c] = 1;
    const int x = c % g.w, y = c / g.w;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy) continue;
        if (!g.free(x + dx, y + dy)) continue;
        if (dx && dy && (!g.free(x + dx, y) || !g.free(x, y + dy))) continue;
        const int nIdx = (y + dy) * g.w + x + dx;
        const float c2 = cost[c] + ((dx && dy) ? float(M_SQRT2) : 1.f);
        if (c2 < cost[nIdx]) {
          cost[nIdx] = c2;
          from[nIdx] = c;
          open.push({c2 + h(nIdx), nIdx});
        }
      }
  }
  std::vector<int> path;
  if (from[goal] < 0 && goal != start) return path;
  for (int c = goal; c >= 0; c = from[c]) path.push_back(c);
  std::reverse(path.begin(), path.end());
  return path;
}

/** Nearest free cells around a point, as component -> squared distance. */
std::vector<std::pair<int, int>> nearbyCells(const Grid& g, int cx, int cy,
                                             int radius) {
  std::vector<std::pair<int, int>> out;  // (distance², cell)
  for (int y = cy - radius; y <= cy + radius; ++y)
    for (int x = cx - radius; x <= cx + radius; ++x)
      if (g.free(x, y))
        out.push_back({(x - cx) * (x - cx) + (y - cy) * (y - cy), y * g.w + x});
  std::sort(out.begin(), out.end());
  return out;
}
}  // namespace

QRectF RoutePlanner::searchArea(double fromLat, double fromLon, double toLat,
                                double toLon) {
  // Both points with a generous margin for detours.
  QRectF area =
      QRectF(geo::toMercator(fromLat, fromLon), geo::toMercator(toLat, toLon))
          .normalized();
  const double nmPerUnit = geo::nmPerMercatorUnit((fromLat + toLat) / 2);
  const double extent = std::max(area.width(), area.height());
  const double pad = std::max(extent * 0.35, 2.0 / nmPerUnit);
  return area.adjusted(-pad, -pad, pad, pad);
}

RoutePlanResult RoutePlanner::plan(const RoutePlanRequest& req) {
  RoutePlanResult res;
  const QPointF a = geo::toMercator(req.fromLat, req.fromLon);
  const QPointF b = geo::toMercator(req.toLat, req.toLon);
  const double midLat = (req.fromLat + req.toLat) / 2;
  const double nmPerUnit = geo::nmPerMercatorUnit(midLat);
  if (geo::distanceNm(req.fromLat, req.fromLon, req.toLat, req.toLon) < 0.01) {
    res.error = tr("Start and destination are the same.");
    return res;
  }

  const QRectF area =
      searchArea(req.fromLat, req.fromLon, req.toLat, req.toLon);
  double cell = std::max(area.width(), area.height()) / req.maxCells;
  cell = std::max(cell, 0.01 / nmPerUnit);  // not finer than ~20 m
  res.cellNm = cell * nmPerUnit;

  Grid g;
  g.w = int(std::ceil(area.width() / cell));
  g.h = int(std::ceil(area.height() / cell));
  QTransform t;
  t.scale(1 / cell, 1 / cell);
  t.translate(-area.left(), -area.top());

  QImage img(g.w, g.h, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  {
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setTransform(t);
    if (req.land) {
      QRegion clip(0, 0, g.w, g.h);
      for (const QRectF& r : req.landIgnored)
        clip -= QRegion(t.mapRect(r).toAlignedRect());
      p.save();
      // The clip region is in device coordinates: set it without transform.
      p.resetTransform();
      p.setClipRegion(clip);
      p.setTransform(t);
      p.setPen(Qt::NoPen);
      p.setBrush(Qt::black);
      for (const MercatorPolygon& poly : *req.land)
        if (poly.bounds.intersects(area)) p.drawPath(poly.path);
      p.restore();
    }
    QPen pen(Qt::black, 2.0);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    for (const QPolygonF& l : req.barriers) p.drawPolyline(l);
    for (const QPolygonF& l : req.coastlines) p.drawPolyline(l);
  }
  g.blocked.resize(size_t(g.w) * g.h);
  for (int y = 0; y < g.h; ++y) {
    const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    for (int x = 0; x < g.w; ++x) g.blocked[y * g.w + x] = qRed(row[x]) < 128;
  }

  // Coastlines: whatever lies on their left is land.
  if (!req.coastlines.isEmpty()) {
    int count = 0;
    const std::vector<int> label = components(g, &count);
    std::vector<int> land(count, 0), water(count, 0);
    const auto seed = [&](const QPointF& p, std::vector<int>& counter) {
      const int x = int(std::floor(p.x())), y = int(std::floor(p.y()));
      if (g.free(x, y)) ++counter[label[y * g.w + x]];
    };
    for (const QPolygonF& line : req.coastlines) {
      const QPolygonF px = t.map(line);
      for (int i = 1; i < px.size(); ++i) {
        const QPointF d = px[i] - px[i - 1];
        const double len = std::hypot(d.x(), d.y());
        if (len < 1e-9) continue;
        // Left of the direction on a north-up map with y pointing down.
        const QPointF left(d.y() / len, -d.x() / len);
        for (double s = 0; s <= len; s += 1.0) {
          const QPointF c = px[i - 1] + d * (s / len);
          seed(c + left * 2.5, land);
          seed(c - left * 2.5, water);
        }
      }
    }
    for (size_t i = 0; i < g.blocked.size(); ++i)
      if (label[i] >= 0 && land[label[i]] > water[label[i]]) g.blocked[i] = 1;
  }

  const QPointF pa = t.map(a), pb = t.map(b);
  const int ax = int(pa.x()), ay = int(pa.y());
  const int bx = int(pb.x()), by = int(pb.y());
  const int searchRadius =
      std::clamp(int(std::ceil(1.0 / res.cellNm)), 10, 120);  // ~1 nm

  const int margin = std::min(3, int(std::round(req.marginNm / res.cellNm)));
  std::vector<int> path;
  Grid used;
  for (int m = margin; m >= 0 && path.empty(); --m) {
    used = dilate(g, m);
    int count = 0;
    const std::vector<int> label = components(used, &count);
    // Pick the water body both ends can reach with the shortest detour.
    std::vector<int> bestA(count, INT_MAX), bestB(count, INT_MAX);
    std::vector<int> cellA(count, -1), cellB(count, -1);
    for (const auto& [d, c] : nearbyCells(used, ax, ay, searchRadius))
      if (d < bestA[label[c]]) {
        bestA[label[c]] = d;
        cellA[label[c]] = c;
      }
    for (const auto& [d, c] : nearbyCells(used, bx, by, searchRadius))
      if (d < bestB[label[c]]) {
        bestB[label[c]] = d;
        cellB[label[c]] = c;
      }
    int best = -1;
    double bestCost = 1e300;
    for (int c = 0; c < count; ++c) {
      if (cellA[c] < 0 || cellB[c] < 0) continue;
      const double cost =
          std::sqrt(double(bestA[c])) + std::sqrt(double(bestB[c]));
      if (cost < bestCost) {
        bestCost = cost;
        best = c;
      }
    }
    if (best < 0) continue;
    path = astar(used, cellA[best], cellB[best]);
  }
  if (path.empty()) {
    res.error =
        tr("No route over water found between start and "
           "destination. Check that both are near navigable water.");
    return res;
  }

  // Straighten: keep only the turning points that are needed, checked
  // against the real obstacles (the margin only steers the search). Start and
  // destination may lie on land (a marina's center); the cells around them
  // up to the snapped water cell do not count as obstacles.
  const auto center = [&](int c) {
    return QPointF(c % used.w + 0.5, c / used.w + 0.5);
  };
  const QVector<QPair<QPointF, double>> ignore{
      {pa, QLineF(pa, center(path.front())).length() + 1.0},
      {pb, QLineF(pb, center(path.back())).length() + 1.0}};
  QVector<QPointF> line{pa};
  for (int c : path) line << center(c);
  line << pb;
  QVector<QPointF> keep{line.first()};
  int i = 0;
  while (i + 1 < line.size()) {
    int j = i + 1;
    while (j + 1 < line.size() && lineClear(g, line[i], line[j + 1], ignore))
      ++j;
    keep << line[j];
    i = j;
  }

  const QTransform inv = t.inverted();
  for (int k = 0; k < keep.size(); ++k) {
    if (k == 0) {
      res.points << QPointF(req.fromLon, req.fromLat);
    } else if (k + 1 == keep.size()) {
      res.points << QPointF(req.toLon, req.toLat);
    } else {
      double lat, lon;
      geo::fromMercator(inv.map(keep[k]), lat, lon);
      res.points << QPointF(lon, lat);
    }
  }
  for (int k = 1; k < res.points.size(); ++k)
    res.lengthNm +=
        geo::distanceNm(res.points[k - 1].y(), res.points[k - 1].x(),
                        res.points[k].y(), res.points[k].x());
  return res;
}
