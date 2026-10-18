#include "core/charts/S57Chart.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include <cmath>
#include <mutex>

#include "core/GeoMath.h"
#include "ogr_geometry.h"
#include "s57.h"
#include "s57class_registrar.h"

extern S57ClassRegistrar* g_poRegistrar;

namespace {

// The ISO 8211 / S-57 reader uses a global class registrar that is not
// thread-safe; all reading is serialized.
std::mutex& readerMutex() {
  static std::mutex m;
  return m;
}

QString& registryPath() {
  static QString p;
  return p;
}

S57ClassRegistrar* registrar() {
  static S57ClassRegistrar* reg = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    auto* r = new S57ClassRegistrar();
    if (r->LoadInfo(registryPath().toLocal8Bit().constData(), 0)) {
      reg = r;
      g_poRegistrar = r;
    } else {
      delete r;
    }
  }
  return reg;
}

const char* const kAttrs[] = {
    "DRVAL1", "DRVAL2", "VALDCO", "VALSOU", "COLOUR", "COLPAT", "BOYSHP",
    "BCNSHP", "CATLAM", "CATCAM", "OBJNAM", "LITCHR", "SIGGRP", "SIGPER",
    "CATWRK", "WATLEV", "SECTR1", "SECTR2", "CATLIT", "VALNMR", "CATOBS",
    "CATLMK", "CATSPM", "ORIENT", "HEIGHT", "CATCOV", "INFORM", "NOBJNM"};

QColor s57Colour(int code, const ChartStyle& s) {
  switch (code) {
    case 1:
      return s.white;
    case 2:
      return s.black;
    case 3:
      return s.red;
    case 4:
      return s.green;
    case 5:
      return QColor(40, 90, 220);
    case 6:
    case 9:
    case 11:
      return s.yellow;
    case 7:
      return s.grey;
    case 8:
      return QColor(140, 90, 40);
    case 10:
    case 12:
    case 13:
      return s.magenta;
    default:
      return s.grey;
  }
}

QString lightDescription(const S57Feature& f) {
  static const QHash<int, QString> kChr = {
      {1, "F"},      {2, "Fl"},      {3, "LFl"},  {4, "Q"},      {5, "VQ"},
      {6, "UQ"},     {7, "Iso"},     {8, "Oc"},   {9, "IQ"},     {10, "IVQ"},
      {11, "IUQ"},   {12, "Mo"},     {13, "FFl"}, {14, "FlLFl"}, {16, "OcFl"},
      {25, "Q+LFl"}, {26, "VQ+LFl"}, {28, "Al"}};
  QString s = kChr.value(f.list("LITCHR").value(0));
  const QString grp = f.attrs.value("SIGGRP");
  if (!grp.isEmpty() && grp != "()" && grp != "(1)") s += grp;
  QString col;
  for (int c : f.list("COLOUR")) {
    if (c == 1) col += "W";
    if (c == 3) col += "R";
    if (c == 4) col += "G";
    if (c == 6) col += "Y";
  }
  if (!col.isEmpty()) s += (s.isEmpty() ? "" : " ") + col;
  const double per = f.num("SIGPER");
  if (!std::isnan(per)) s += QStringLiteral(" %1s").arg(per);
  return s.trimmed();
}

QString depthText(double d) {
  if (d < 31 && std::fabs(d - std::round(d)) > 0.05)
    return QString::number(d, 'f', 1);
  return QString::number(int(std::floor(d + 0.05)));
}

void haloText(QPainter& p, const QPointF& pos, const QString& text,
              const QFont& font, const QColor& fg, const QColor& halo) {
  QPainterPath path;
  path.addText(pos, font, text);
  p.setPen(QPen(halo, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);
  p.setPen(Qt::NoPen);
  p.setBrush(fg);
  p.drawPath(path);
}

}  // namespace

// --------------------------------------------------------------------------

double S57Feature::num(const char* key, double fallback) const {
  bool ok = false;
  const double v = attrs.value(key).toDouble(&ok);
  return ok ? v : fallback;
}

QVector<int> S57Feature::list(const char* key) const {
  QVector<int> out;
  static const QRegularExpression re("\\d+");
  auto it = re.globalMatch(attrs.value(key));
  while (it.hasNext()) out << it.next().captured(0).toInt();
  return out;
}

void S57Chart::setRegistryDir(const QString& dir) { registryPath() = dir; }
QString S57Chart::registryDir() { return registryPath(); }

S57Chart::S57Chart(const QString& path) {
  m_path = path;
  m_name = QFileInfo(path).completeBaseName();
  std::lock_guard<std::mutex> lock(m_loadMutex);
  m_valid = read(false);
  m_loaded = m_valid;
}

bool S57Chart::loadData() const { return read(false); }

void S57Chart::unload() const {
  std::lock_guard<std::mutex> lock(m_loadMutex);
  m_features.clear();
  m_features.squeeze();
  m_loaded = false;
}

const QVector<S57Feature>& S57Chart::features() const {
  ensureLoaded();
  return m_features;
}

bool S57Chart::read(bool /*metadataOnly*/) const {
  std::lock_guard<std::mutex> lock(readerMutex());
  S57ClassRegistrar* reg = registrar();
  auto* self = const_cast<S57Chart*>(this);
  if (!reg) {
    self->m_error = QStringLiteral("S-57 class registry not found in %1")
                        .arg(registryPath());
    return false;
  }
  const QByteArray file = QFile::encodeName(m_path);
  S57Reader reader(file.constData());
  char opt0[] = "LNAM_REFS=ON", opt1[] = "SPLIT_MULTIPOINT=OFF",
       opt2[] = "ADD_SOUNDG_DEPTH=ON", opt3[] = "RETURN_PRIMITIVES=OFF",
       opt4[] = "RETURN_LINKAGES=OFF", opt5[] = "UPDATES=APPLY";
  char* opts[] = {opt0, opt1, opt2, opt3, opt4, opt5, nullptr};
  reader.SetOptions(opts);
  reader.SetClassBased(reg);
  if (!reader.Open(FALSE)) {
    self->m_error = QStringLiteral("Cannot open S-57 file");
    return false;
  }
  static QVector<OGRFeatureDefn*> defns;  // shared, owned forever
  if (defns.isEmpty()) {
    for (int i = 0; i < 2000; ++i)
      if (reg->SelectClass(i))
        if (OGRFeatureDefn* d =
                S57GenerateObjectClassDefn(reg, i, reader.GetOptionFlags()))
          defns << d;
  }
  for (OGRFeatureDefn* d : defns) reader.AddFeatureDefn(d);
  // Ingest() returns the update status, not success; an empty feature
  // list below signals a broken file.
  reader.Ingest();
  self->m_scale = reader.GetCSCL();
  if (const char* dsnm = reader.GetDSNM())
    if (*dsnm)
      self->m_name = QFileInfo(QString::fromLatin1(dsnm)).completeBaseName();

  QVector<S57Feature> out;
  QPainterPath coverage;
  QRectF bounds;
  while (OGRFeature* f = reader.ReadNextFeature()) {
    S57Feature sf;
    sf.cls = f->GetDefnRef()->GetName();
    for (const char* key : kAttrs) {
      const int idx = f->GetFieldIndex(key);
      if (idx >= 0 && f->IsFieldSet(idx)) {
        const QString v = QString::fromLatin1(f->GetFieldAsString(idx));
        if (!v.isEmpty()) sf.attrs.insert(key, v);
      }
    }
    OGRGeometry* g = f->GetGeometryRef();
    bool ok = g != nullptr;
    if (ok) {
      switch (wkbFlatten(g->getGeometryType())) {
        case wkbPoint: {
          auto* pt = static_cast<OGRPoint*>(g);
          sf.geom = S57Feature::Geom::Point;
          sf.point = geo::toMercator(pt->getY(), pt->getX());
          break;
        }
        case wkbMultiPoint: {
          auto* mp = static_cast<OGRMultiPoint*>(g);
          sf.geom = S57Feature::Geom::Soundings;
          for (int i = 0; i < mp->getNumGeometries(); ++i) {
            auto* pt = static_cast<OGRPoint*>(mp->getGeometryRef(i));
            sf.soundPts << geo::toMercator(pt->getY(), pt->getX());
            sf.soundDepths << float(pt->getZ());
          }
          break;
        }
        case wkbLineString:
        case wkbMultiLineString: {
          sf.geom = S57Feature::Geom::Line;
          auto addLine = [&](OGRLineString* ls) {
            for (int i = 0; i < ls->getNumPoints(); ++i) {
              const QPointF m = geo::toMercator(ls->getY(i), ls->getX(i));
              if (i == 0)
                sf.path.moveTo(m);
              else
                sf.path.lineTo(m);
            }
          };
          if (wkbFlatten(g->getGeometryType()) == wkbLineString) {
            addLine(static_cast<OGRLineString*>(g));
          } else {
            auto* ml = static_cast<OGRMultiLineString*>(g);
            for (int i = 0; i < ml->getNumGeometries(); ++i)
              addLine(static_cast<OGRLineString*>(ml->getGeometryRef(i)));
          }
          break;
        }
        case wkbPolygon: {
          auto* poly = static_cast<OGRPolygon*>(g);
          sf.geom = S57Feature::Geom::Area;
          sf.path.setFillRule(Qt::OddEvenFill);
          auto addRing = [&](const OGRLinearRing* r) {
            if (!r) return;
            QPolygonF ring;
            for (int i = 0; i < r->getNumPoints(); ++i)
              ring << geo::toMercator(r->getY(i), r->getX(i));
            sf.path.addPolygon(ring);
          };
          addRing(poly->getExteriorRing());
          for (int i = 0; i < poly->getNumInteriorRings(); ++i)
            addRing(poly->getInteriorRing(i));
          break;
        }
        default:
          ok = false;
      }
    }
    delete f;
    if (!ok) continue;
    if (sf.cls == "M_COVR") {
      if (sf.list("CATCOV").value(0, 1) == 1 &&
          sf.geom == S57Feature::Geom::Area)
        coverage.addPath(sf.path);
      continue;
    }
    if (sf.cls.startsWith("M_")) continue;  // other meta objects
    if (sf.geom == S57Feature::Geom::Point)
      bounds |= QRectF(sf.point, QSizeF(1e-9, 1e-9));
    else if (sf.geom == S57Feature::Geom::Soundings)
      for (const QPointF& pt : sf.soundPts)
        bounds |= QRectF(pt, QSizeF(1e-9, 1e-9));
    else
      bounds |= sf.path.boundingRect();
    out.push_back(std::move(sf));
  }
  if (coverage.isEmpty()) coverage.addRect(bounds);
  self->m_coverage = coverage;
  self->m_bounds = coverage.boundingRect();
  m_features = std::move(out);
  return !m_features.isEmpty();
}

QVector<const S57Feature*> S57Chart::featuresAt(const QPointF& w,
                                                double tol) const {
  QVector<const S57Feature*> out;
  for (const auto& f : features()) {
    switch (f.geom) {
      case S57Feature::Geom::Point:
        if (std::hypot(f.point.x() - w.x(), f.point.y() - w.y()) < tol)
          out << &f;
        break;
      case S57Feature::Geom::Area:
        if (f.path.contains(w)) out << &f;
        break;
      default:
        break;
    }
  }
  return out;
}

// --------------------------------------------------------------------------
// Rendering

void S57Chart::render(QPainter& p, const ChartRenderContext& ctx) const {
  if (!m_valid || !ensureLoaded()) return;
  const ChartStyle& s = ctx.style;
  const QTransform& t = ctx.worldToImage;
  const QRectF vis = ctx.visibleWorld;
  const bool detail = ctx.displayScale < m_scale * 2.5;
  const bool labels = ctx.displayScale < m_scale * 1.5;

  auto visible = [&](const S57Feature& f) {
    if (f.geom == S57Feature::Geom::Point) return vis.contains(f.point);
    return f.path.isEmpty() || f.path.controlPointRect().intersects(vis);
  };
  auto cosmetic = [](const QColor& c, qreal w,
                     Qt::PenStyle st = Qt::SolidLine) {
    QPen pen(c, w, st, Qt::RoundCap, Qt::RoundJoin);
    pen.setCosmetic(true);
    return pen;
  };

  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setTransform(t);
  p.setClipPath(m_coverage);

  // Safety contour: the shallowest contour at or below the safety depth.
  double safetyContour = 1e9;
  for (const auto& f : m_features)
    if (f.cls == "DEPCNT") {
      const double v = f.num("VALDCO");
      if (v >= s.safetyDepth && v < safetyContour) safetyContour = v;
    }

  // --- Areas -------------------------------------------------------------
  for (const auto& f : m_features) {
    if (f.geom != S57Feature::Geom::Area || !visible(f)) continue;
    QColor fill;
    if (f.cls == "DEPARE" || f.cls == "DRGARE") {
      const double d1 = f.num("DRVAL1", 0);
      const double d2 = f.num("DRVAL2", d1);
      if (d1 < 0)
        fill = s.depthIntertidal;
      else if (d2 <= s.shallowDepth)
        fill = s.depthVeryShallow;
      else if (d2 <= s.safetyDepth)
        fill = s.depthShallow;
      else if (d1 < s.deepDepth)
        fill = s.depthMedium;
      else
        fill = s.depthDeep;
    } else if (f.cls == "UNSARE") {
      fill = s.depthShallow;
    } else if (f.cls == "LNDARE") {
      fill = s.land;
    }
    if (!fill.isValid()) continue;
    p.setPen(cosmetic(fill, 1));
    p.setBrush(fill);
    p.drawPath(f.path);
  }
  for (const auto& f : m_features) {
    if (f.geom != S57Feature::Geom::Area || !visible(f)) continue;
    if (f.cls == "BUAARE") {
      p.setPen(Qt::NoPen);
      p.setBrush(s.built);
      p.drawPath(f.path);
    } else if (f.cls == "BUISGL" || f.cls == "SLCONS" || f.cls == "PONTON" ||
               f.cls == "BRIDGE" || f.cls == "HULKES" || f.cls == "SILTNK" ||
               f.cls == "OFSPLF") {
      p.setPen(cosmetic(s.landEdge, 1));
      p.setBrush(s.structure);
      p.drawPath(f.path);
    } else if (f.cls == "DRGARE") {
      p.setPen(cosmetic(s.grey, 1, Qt::DashLine));
      p.setBrush(Qt::NoBrush);
      p.drawPath(f.path);
    } else if (detail &&
               (f.cls == "RESARE" || f.cls == "ACHARE" || f.cls == "CTNARE" ||
                f.cls == "CBLARE" || f.cls == "PIPARE" || f.cls == "MIPARE")) {
      QColor c = s.magenta;
      c.setAlpha(170);
      p.setPen(cosmetic(c, 1.2, Qt::DashLine));
      p.setBrush(Qt::NoBrush);
      p.drawPath(f.path);
    } else if (f.cls == "FAIRWY") {
      QColor c = s.grey;
      c.setAlpha(150);
      p.setPen(cosmetic(c, 1, Qt::DashDotLine));
      p.setBrush(Qt::NoBrush);
      p.drawPath(f.path);
    }
  }

  // --- Lines -------------------------------------------------------------
  for (const auto& f : m_features) {
    if (!visible(f)) continue;
    const bool line = f.geom == S57Feature::Geom::Line;
    if (f.cls == "DEPCNT" && line) {
      const bool safety = f.num("VALDCO") == safetyContour;
      p.setPen(safety ? cosmetic(s.safetyContour, 2)
                      : cosmetic(s.contour, 0.8));
    } else if (f.cls == "COALNE" || (f.cls == "SLCONS" && line)) {
      p.setPen(cosmetic(s.coast, 1.3));
    } else if (f.cls == "LNDARE" && f.geom == S57Feature::Geom::Area) {
      p.setPen(cosmetic(s.coast, 1.1));
    } else if (detail && (f.cls == "CBLSUB" || f.cls == "CBLOHD" ||
                          f.cls == "PIPSOL" || f.cls == "PIPOHD")) {
      p.setPen(cosmetic(s.magenta, 1, Qt::DashLine));
    } else if (f.cls == "NAVLNE" || f.cls == "RECTRC") {
      p.setPen(cosmetic(s.text, 1, Qt::DashLine));
    } else if (f.cls == "RIVERS" && line) {
      p.setPen(cosmetic(s.depthShallow, 1.2));
    } else {
      continue;
    }
    p.setBrush(Qt::NoBrush);
    p.drawPath(f.path);
  }

  // --- Points (drawn in device pixels) -------------------------------------
  p.setTransform(QTransform());
  p.setClipping(false);
  if (detail) {
    const QFont sf = [] {
      QFont f;
      f.setPixelSize(10);
      f.setItalic(true);
      return f;
    }();
    p.setFont(sf);
    for (const auto& f : m_features) {
      if (f.cls != "SOUNDG" || f.geom != S57Feature::Geom::Soundings) continue;
      for (int i = 0; i < f.soundPts.size(); ++i) {
        if (!vis.contains(f.soundPts[i])) continue;
        if (!m_coverage.contains(f.soundPts[i])) continue;
        const double d = f.soundDepths[i];
        p.setPen(d <= s.safetyDepth ? s.soundingShallow : s.soundingDeep);
        const QPointF sp = t.map(f.soundPts[i]);
        p.drawText(QRectF(sp.x() - 20, sp.y() - 7, 40, 14), Qt::AlignCenter,
                   depthText(d));
      }
    }
  }
  for (const auto& f : m_features) {
    if (f.geom != S57Feature::Geom::Point || !vis.contains(f.point)) continue;
    drawSymbol(p, f, t.map(f.point), ctx);
  }
  if (labels) {
    QFont lf;
    lf.setPixelSize(11);
    lf.setItalic(true);
    for (const auto& f : m_features) {
      if (f.geom != S57Feature::Geom::Area || !visible(f)) continue;
      if (f.cls != "SEAARE" && f.cls != "LNDRGN") continue;
      const QString name = f.attrs.value("OBJNAM");
      if (name.isEmpty()) continue;
      const QPointF c = t.map(f.path.boundingRect().center());
      haloText(p, c, name, lf, s.text, s.textHalo);
    }
  }
  p.restore();
}

void S57Chart::drawSymbol(QPainter& p, const S57Feature& f, const QPointF& pos,
                          const ChartRenderContext& ctx) const {
  const ChartStyle& s = ctx.style;
  const QByteArray& c = f.cls;
  const QColor outline = s.dark ? QColor(200, 205, 215) : QColor(20, 20, 20);
  const QVector<int> colours = f.list("COLOUR");
  const bool detail = ctx.displayScale < m_scale * 2.5;

  auto bands = [&](const QPainterPath& shape) {
    // Horizontal colour bands (e.g. cardinal marks).
    const QRectF r = shape.boundingRect();
    const int n = std::max<int>(1, colours.size());
    p.save();
    p.setClipPath(shape);
    for (int i = 0; i < n; ++i) {
      p.fillRect(QRectF(r.left(), r.top() + r.height() * i / n, r.width(),
                        r.height() / n + 0.5),
                 s57Colour(colours.value(i, 7), s));
    }
    p.restore();
    p.setPen(QPen(outline, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);
  };

  if (c.startsWith("BOY")) {
    const int shape = f.list("BOYSHP").value(0, 4);
    QPainterPath body;
    const QPointF b = pos;
    switch (shape) {
      case 1:  // conical
        body.moveTo(b + QPointF(-5, 0));
        body.lineTo(b + QPointF(0, -12));
        body.lineTo(b + QPointF(5, 0));
        body.closeSubpath();
        break;
      case 2:  // can
        body.addRect(QRectF(b.x() - 5, b.y() - 10, 10, 10));
        break;
      case 3:  // spherical
        body.addEllipse(QPointF(b.x(), b.y() - 5), 5, 5);
        break;
      default:  // pillar / spar / barrel
        body.moveTo(b + QPointF(-4, 0));
        body.lineTo(b + QPointF(-1, -14));
        body.lineTo(b + QPointF(3, -14));
        body.lineTo(b + QPointF(4, 0));
        body.closeSubpath();
    }
    bands(body);
    p.setPen(QPen(outline, 1.2));
    p.drawLine(b + QPointF(-7, 0), b + QPointF(7, 0));
    p.setBrush(outline);
    p.drawEllipse(b, 1.5, 1.5);
  } else if (c.startsWith("BCN")) {
    QPainterPath top;
    top.addRect(QRectF(pos.x() - 3.5, pos.y() - 14, 7, 7));
    p.setPen(QPen(outline, 1.5));
    p.drawLine(pos, pos + QPointF(0, -8));
    bands(top);
    p.drawEllipse(pos, 1.5, 1.5);
  } else if (c == "LIGHTS") {
    QColor lc = s.magenta;
    for (int col : colours) {
      if (col == 3) lc = s.red;
      if (col == 4) lc = s.green;
    }
    const double s1 = f.num("SECTR1"), s2 = f.num("SECTR2");
    if (detail && !std::isnan(s1) && !std::isnan(s2)) {
      // Sector light: legs are given from seaward, true bearing.
      const double r = 28;
      QColor legs = s.text;
      legs.setAlpha(150);
      p.setPen(QPen(legs, 1, Qt::DashLine));
      for (double a : {s1, s2}) {
        const double rad = (a + 180 - ctx.rotation) * M_PI / 180;
        p.drawLine(pos, pos + QPointF(std::sin(rad), -std::cos(rad)) * r * 1.6);
      }
      double span = std::fmod(s2 - s1 + 360, 360);
      if (span == 0) span = 360;
      QPen arc(lc, 4, Qt::SolidLine, Qt::FlatCap);
      p.setPen(arc);
      p.setBrush(Qt::NoBrush);
      // Qt angles: 0 = 3 o'clock, counter-clockwise.
      const double startQt = 90 - (s1 + 180 - ctx.rotation);
      p.drawArc(QRectF(pos.x() - r, pos.y() - r, 2 * r, 2 * r),
                int(startQt * 16), int(-span * 16));
    } else {
      // Classic light flare.
      p.save();
      p.translate(pos);
      p.rotate(135);
      QPainterPath flare;
      flare.moveTo(0, 0);
      flare.cubicTo(-4, -6, -4, -14, 0, -18);
      flare.cubicTo(4, -14, 4, -6, 0, 0);
      p.setPen(
          QPen(s.dark ? QColor(0, 0, 0, 120) : QColor(255, 255, 255), 0.8));
      p.setBrush(lc);
      p.drawPath(flare);
      p.restore();
    }
    if (ctx.displayScale < m_scale * 1.2) {
      QFont lf;
      lf.setPixelSize(10);
      haloText(p, pos + QPointF(10, 14), lightDescription(f), lf, s.text,
               s.textHalo);
    }
  } else if (c == "WRECKS" || c == "UWTROC" || c == "OBSTRN") {
    const double depth = f.num("VALSOU");
    const bool danger = std::isnan(depth) || depth <= s.safetyDepth;
    p.setBrush(Qt::NoBrush);
    if (danger) {
      QPen dots(outline, 1.2, Qt::DotLine);
      p.setPen(dots);
      p.drawEllipse(pos, 8, 8);
    }
    p.setPen(QPen(outline, 1.4));
    if (c == "WRECKS") {
      p.drawLine(pos + QPointF(-7, 0), pos + QPointF(7, 0));
      for (int dx : {-4, 0, 4})
        p.drawLine(pos + QPointF(dx, -3), pos + QPointF(dx, 3));
    } else if (c == "UWTROC") {
      p.drawLine(pos + QPointF(-4, 0), pos + QPointF(4, 0));
      p.drawLine(pos + QPointF(0, -4), pos + QPointF(0, 4));
    } else if (!danger) {
      p.setPen(QPen(outline, 1.2, Qt::DotLine));
      p.drawEllipse(pos, 6, 6);
    }
    if (!std::isnan(depth) && detail) {
      QFont df;
      df.setPixelSize(9);
      df.setItalic(true);
      p.setFont(df);
      p.setPen(s.soundingShallow);
      p.drawText(QRectF(pos.x() - 20, pos.y() + 8, 40, 12), Qt::AlignCenter,
                 depthText(depth));
    }
  } else if (c == "LNDMRK") {
    p.setPen(QPen(outline, 1.3));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(pos, 5, 5);
    p.setBrush(outline);
    p.drawEllipse(pos, 1.5, 1.5);
  } else if (c == "PILPNT" && detail) {
    p.setPen(Qt::NoPen);
    p.setBrush(outline);
    p.drawEllipse(pos, 1.6, 1.6);
  } else if (c == "MORFAC" && detail) {
    p.setPen(QPen(outline, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(pos.x() - 3, pos.y() - 3, 6, 6));
  }
}
