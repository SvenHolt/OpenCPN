#include "core/GpxIO.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {

Waypoint readPoint(QXmlStreamReader& x) {
  Waypoint w;
  const QString tag = x.name().toString();
  w.lat = x.attributes().value("lat").toDouble();
  w.lon = x.attributes().value("lon").toDouble();
  while (x.readNextStartElement()) {
    const auto n = x.name();
    if (n == u"name")
      w.name = x.readElementText();
    else if (n == u"sym")
      w.symbol = x.readElementText();
    else if (n == u"desc")
      w.description = x.readElementText();
    else if (n == u"time")
      w.time = QDateTime::fromString(x.readElementText(), Qt::ISODate);
    else if (n == u"extensions") {
      while (x.readNextStartElement()) {
        if (x.name() == u"guid")
          w.id = x.readElementText();
        else
          x.skipCurrentElement();
      }
    } else
      x.skipCurrentElement();
  }
  Q_UNUSED(tag);
  if (w.id.isEmpty()) w.id = NavStore::newId();
  return w;
}

Route readRoute(QXmlStreamReader& x, const char* pointTag) {
  Route r;
  while (x.readNextStartElement()) {
    const auto n = x.name();
    if (n == u"name")
      r.name = x.readElementText();
    else if (n == QLatin1String(pointTag))
      r.points.push_back(readPoint(x));
    else if (n == u"trkseg") {
      while (x.readNextStartElement()) {
        if (x.name() == u"trkpt")
          r.points.push_back(readPoint(x));
        else
          x.skipCurrentElement();
      }
    } else if (n == u"extensions") {
      while (x.readNextStartElement()) {
        if (x.name() == u"guid")
          r.id = x.readElementText();
        else
          x.skipCurrentElement();
      }
    } else
      x.skipCurrentElement();
  }
  if (r.id.isEmpty()) r.id = NavStore::newId();
  return r;
}

void writePoint(QXmlStreamWriter& w, const char* tag, const Waypoint& p) {
  w.writeStartElement(tag);
  w.writeAttribute("lat", QString::number(p.lat, 'f', 9));
  w.writeAttribute("lon", QString::number(p.lon, 'f', 9));
  if (p.time.isValid())
    w.writeTextElement("time", p.time.toUTC().toString(Qt::ISODate));
  if (QByteArray(tag) == "trkpt") {
    w.writeEndElement();
    return;
  }
  w.writeTextElement("name", p.name);
  if (!p.description.isEmpty()) w.writeTextElement("desc", p.description);
  w.writeTextElement("sym", p.symbol);
  w.writeStartElement("extensions");
  w.writeTextElement("opencpn:guid", p.id);
  w.writeEndElement();
  w.writeEndElement();
}

}  // namespace

namespace GpxIO {

bool parse(const QByteArray& xml, GpxData& out, QString* error) {
  QXmlStreamReader x(xml);
  if (!x.readNextStartElement() || x.name() != u"gpx") {
    if (error) *error = QStringLiteral("Not a GPX document");
    return false;
  }
  while (x.readNextStartElement()) {
    const auto n = x.name();
    if (n == u"wpt")
      out.waypoints.push_back(readPoint(x));
    else if (n == u"rte")
      out.routes.push_back(readRoute(x, "rtept"));
    else if (n == u"trk")
      out.tracks.push_back(readRoute(x, "trkpt"));
    else
      x.skipCurrentElement();
  }
  if (x.hasError()) {
    if (error) *error = x.errorString();
    return false;
  }
  return true;
}

bool read(const QString& path, GpxData& out, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = f.errorString();
    return false;
  }
  return parse(f.readAll(), out, error);
}

QByteArray serialize(const GpxData& d) {
  QByteArray buf;
  QXmlStreamWriter w(&buf);
  w.setAutoFormatting(true);
  w.writeStartDocument();
  w.writeStartElement("gpx");
  w.writeAttribute("version", "1.1");
  w.writeAttribute("creator", "OpenCPN Qt");
  w.writeDefaultNamespace("http://www.topografix.com/GPX/1/1");
  w.writeNamespace("http://www.opencpn.org", "opencpn");
  for (const auto& p : d.waypoints) writePoint(w, "wpt", p);
  for (const auto& r : d.routes) {
    w.writeStartElement("rte");
    w.writeTextElement("name", r.name);
    w.writeStartElement("extensions");
    w.writeTextElement("opencpn:guid", r.id);
    w.writeEndElement();
    for (const auto& p : r.points) writePoint(w, "rtept", p);
    w.writeEndElement();
  }
  for (const auto& t : d.tracks) {
    w.writeStartElement("trk");
    w.writeTextElement("name", t.name);
    w.writeStartElement("extensions");
    w.writeTextElement("opencpn:guid", t.id);
    w.writeEndElement();
    w.writeStartElement("trkseg");
    for (const auto& p : t.points) writePoint(w, "trkpt", p);
    w.writeEndElement();
    w.writeEndElement();
  }
  w.writeEndElement();
  w.writeEndDocument();
  return buf;
}

bool write(const QString& path, const GpxData& d) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(serialize(d));
  return f.commit();
}

}  // namespace GpxIO
