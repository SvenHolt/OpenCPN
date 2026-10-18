#include "core/Ais.h"

#include <vector>

namespace {

class BitReader {
public:
  explicit BitReader(const QString& payload) {
    m_bits.reserve(payload.size() * 6);
    for (QChar ch : payload) {
      int v = ch.unicode() - 48;
      if (v > 40) v -= 8;
      for (int i = 5; i >= 0; --i) m_bits.push_back((v >> i) & 1);
    }
  }
  int size() const { return int(m_bits.size()); }
  quint32 u(int start, int len) const {
    quint32 r = 0;
    for (int i = 0; i < len; ++i) {
      r <<= 1;
      if (start + i < size()) r |= m_bits[start + i];
    }
    return r;
  }
  qint32 s(int start, int len) const {
    quint32 r = u(start, len);
    if (r & (1u << (len - 1))) r |= ~((1u << len) - 1);
    return qint32(r);
  }
  QString text(int start, int chars) const {
    QString out;
    for (int i = 0; i < chars; ++i) {
      if (start + 6 * (i + 1) > size()) break;
      int c = int(u(start + 6 * i, 6));
      out.append(QChar(c < 32 ? c + 64 : c));
    }
    while (out.endsWith('@') || out.endsWith(' ')) out.chop(1);
    int at = out.indexOf('@');
    if (at >= 0) out.truncate(at);
    return out.trimmed();
  }

private:
  std::vector<quint8> m_bits;
};

void decodePosition(const BitReader& b, AisMessage& m, int sogBit, int lonBit,
                    int latBit, int cogBit, int hdgBit) {
  const quint32 sog = b.u(sogBit, 10);
  if (sog != 1023) m.sog = sog / 10.0;
  const qint32 lon = b.s(lonBit, 28);
  const qint32 lat = b.s(latBit, 27);
  if (lon != 0x6791AC0 && lat != 0x3412140) {
    m.lon = lon / 600000.0;
    m.lat = lat / 600000.0;
  }
  const quint32 cog = b.u(cogBit, 12);
  if (cog < 3600) m.cog = cog / 10.0;
  const quint32 hdg = b.u(hdgBit, 9);
  if (hdg < 360) m.hdg = hdg;
}

}  // namespace

std::optional<AisMessage> AisDecoder::decodePayload(const QString& payload) {
  if (payload.isEmpty()) return std::nullopt;
  BitReader b(payload);
  AisMessage m;
  m.type = int(b.u(0, 6));
  m.mmsi = b.u(8, 30);
  switch (m.type) {
    case 1:
    case 2:
    case 3:
      if (b.size() < 137) return std::nullopt;
      m.navStatus = int(b.u(38, 4));
      decodePosition(b, m, 50, 61, 89, 116, 128);
      break;
    case 18:
      if (b.size() < 133) return std::nullopt;
      decodePosition(b, m, 46, 57, 85, 112, 124);
      break;
    case 19:
      if (b.size() < 271) return std::nullopt;
      decodePosition(b, m, 46, 57, 85, 112, 124);
      m.name = b.text(143, 20);
      m.shipType = int(b.u(263, 8));
      break;
    case 5:
      if (b.size() < 420) return std::nullopt;
      m.callsign = b.text(70, 7);
      m.name = b.text(112, 20);
      m.shipType = int(b.u(232, 8));
      break;
    case 24: {
      const int part = int(b.u(38, 2));
      if (part == 0) {
        m.name = b.text(40, 20);
      } else if (part == 1) {
        m.shipType = int(b.u(40, 8));
        m.callsign = b.text(90, 7);
      }
      break;
    }
    default:
      return std::nullopt;
  }
  return m;
}

std::optional<AisMessage> AisDecoder::addFragment(const QStringList& f) {
  // !AIVDM,<count>,<index>,<seq>,<channel>,<payload>,<fill>
  if (f.size() < 7) return std::nullopt;
  const bool own = f[0].endsWith("VDO");
  const int count = f[1].toInt();
  const int index = f[2].toInt();
  QString payload;
  if (count <= 1) {
    payload = f[5];
  } else {
    const QString key = f[3] + '/' + f[4];
    QStringList& parts = m_pending[key];
    if (index == 1) parts.clear();
    if (parts.size() != index - 1) {
      m_pending.remove(key);
      return std::nullopt;
    }
    parts << f[5];
    if (index < count) return std::nullopt;
    payload = parts.join(QString());
    m_pending.remove(key);
  }
  auto m = decodePayload(payload);
  if (m) m->own = own;
  return m;
}
