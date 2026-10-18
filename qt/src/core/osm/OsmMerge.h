// Merges OSM XML documents (e.g. API tiles) into one file with unique,
// id-sorted nodes, ways and relations as required by Overpass'
// update_database.
#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QString>
#include <map>

class OsmMerger {
public:
  /** Adds one OSM XML document. Returns false on a parse error. */
  bool add(const QByteArray& xml, QString* error = nullptr);
  /** Writes the merged document. */
  void write(QIODevice* out) const;
  QByteArray toByteArray() const;

  qsizetype nodeCount() const { return qsizetype(m_nodes.size()); }
  qsizetype wayCount() const { return qsizetype(m_ways.size()); }
  qsizetype relationCount() const { return qsizetype(m_relations.size()); }

private:
  std::map<qint64, QByteArray> m_nodes, m_ways, m_relations;
};
