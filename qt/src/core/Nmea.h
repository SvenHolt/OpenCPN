// NMEA 0183 sentence parsing and generation.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <optional>

#include "core/Ais.h"

/** Partial own-ship navigation update decoded from one sentence. */
struct NavUpdate {
  std::optional<double> lat, lon, sog, cog, hdg;
  std::optional<int> satellites;
  std::optional<QDateTime> time;
  bool fixLost = false;
  // Environment: apparent wind angle (-180..180, + = starboard) and speed
  // (kn), true wind angle/speed/direction, speed through water (kn), depth (m)
  std::optional<double> awa, aws, twa, tws, twd, stw, depth;
  // Engine (RPM, XDR): rpm, coolant °C, oil bar, fuel %, hours, volt, l/h
  std::optional<double> rpm, coolantC, oilBar, fuelPct, engineHours, voltage,
      fuelRateLph;
};

class NmeaParser : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;

  /** Feeds raw stream bytes; complete lines are parsed. */
  void feed(const QByteArray& data);
  /** Parses one sentence. Returns true if it was understood. */
  bool parseLine(const QString& line);

  static bool verifyChecksum(const QString& sentence);
  /** "GPRMC,..." -> "$GPRMC,...*hh" */
  static QString makeSentence(const QString& body, QChar start = '$');
  static std::optional<double> parseLatLon(const QString& value,
                                           const QString& hemi);

signals:
  void navUpdate(const NavUpdate& update);
  void aisMessage(const AisMessage& msg);
  void sentenceReceived(const QString& sentence);

private:
  QByteArray m_buffer;
  AisDecoder m_ais;
};
