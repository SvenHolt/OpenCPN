// Minimal AIVDM/AIVDO decoder (message types 1, 2, 3, 5, 18, 19, 24).
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>

struct AisMessage {
  int type = 0;
  quint32 mmsi = 0;
  bool own = false;  // from AIVDO
  std::optional<double> lat, lon, sog, cog, hdg;
  std::optional<int> navStatus, shipType;
  std::optional<QString> name, callsign;
};

class AisDecoder {
public:
  /** Feeds the comma separated fields of one AIVDM/AIVDO sentence (without
   *  checksum). Returns a message once all fragments have arrived. */
  std::optional<AisMessage> addFragment(const QStringList& fields);

  /** Decodes a complete armored payload. */
  static std::optional<AisMessage> decodePayload(const QString& payload);

private:
  QHash<QString, QStringList> m_pending;  // key: seq id + channel
};
