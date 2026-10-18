#include "core/osm/OsmMerge.h"

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {
/** Copies the current element (start tag to matching end) as XML. */
QByteArray copyElement(QXmlStreamReader& r) {
  QByteArray out;
  QXmlStreamWriter w(&out);
  int depth = 0;
  do {
    if (r.isStartElement()) ++depth;
    if (r.isEndElement()) --depth;
    if (r.isStartElement() || r.isEndElement()) w.writeCurrentToken(r);
    if (depth == 0) break;
  } while (!r.atEnd() && r.readNext() != QXmlStreamReader::Invalid);
  return out;
}
}  // namespace

bool OsmMerger::add(const QByteArray& xml, QString* error) {
  QXmlStreamReader r(xml);
  if (!r.readNextStartElement() || r.name() != u"osm") {
    if (error) *error = QStringLiteral("Not an OSM XML document");
    return false;
  }
  while (r.readNextStartElement()) {
    const auto name = r.name();
    std::map<qint64, QByteArray>* target = nullptr;
    if (name == u"node")
      target = &m_nodes;
    else if (name == u"way")
      target = &m_ways;
    else if (name == u"relation")
      target = &m_relations;
    if (!target) {
      r.skipCurrentElement();
      continue;
    }
    const qint64 id = r.attributes().value("id").toLongLong();
    (*target)[id] = copyElement(r);
  }
  if (r.hasError()) {
    if (error) *error = r.errorString();
    return false;
  }
  return true;
}

void OsmMerger::write(QIODevice* out) const {
  out->write(
      "<?xml version='1.0' encoding='UTF-8'?>\n"
      "<osm version=\"0.6\" generator=\"OpenCPN Qt\">\n");
  for (const auto* m : {&m_nodes, &m_ways, &m_relations})
    for (const auto& [id, xml] : *m) {
      out->write(xml);
      out->write("\n");
    }
  out->write("</osm>\n");
}

QByteArray OsmMerger::toByteArray() const {
  QByteArray b;
  QBuffer buf(&b);
  buf.open(QIODevice::WriteOnly);
  write(&buf);
  return b;
}
