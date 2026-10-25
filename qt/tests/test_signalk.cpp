// Signal K, radar and sonar: the WebSocket client against a small fake
// Signal K server (delta stream, radar API, binary spoke stream), delta
// mapping into the NavStore, the radar protobuf, image and simulator, and
// the sonar history.
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainterPath>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include "core/Connections.h"
#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "core/Radar.h"
#include "core/SignalK.h"
#include "core/Sonar.h"
#include "core/WebSocket.h"

namespace {
/** Strongest alpha in a 5×5 box around a pixel. */
int alphaNear(const QImage& img, QPoint c) {
  int a = 0;
  for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx)
      if (img.rect().contains(c + QPoint(dx, dy)))
        a = std::max(a, qAlpha(img.pixel(c + QPoint(dx, dy))));
  return a;
}

/** Answers HTTP GETs and WebSocket upgrades like a Signal K server. */
class FakeServer : public QTcpServer {
public:
  QStringList deltas;     // sent after the hello on the delta stream
  QByteArray radarFrame;  // sent on the radar stream
  QStringList received;   // text frames from clients
  QStringList requests;

  FakeServer() {
    listen(QHostAddress::LocalHost);
    connect(this, &QTcpServer::newConnection, this, [this] {
      while (QTcpSocket* s = nextPendingConnection()) handle(s);
    });
  }
  quint16 port() const { return serverPort(); }

private:
  void handle(QTcpSocket* s) {
    auto* buf = new QByteArray;
    auto* parser = new WebSocketClient::Parser;
    auto* upgraded = new bool(false);
    connect(s, &QTcpSocket::disconnected, s, [s, buf, parser, upgraded] {
      delete buf;
      delete parser;
      delete upgraded;
      s->deleteLater();
    });
    connect(s, &QTcpSocket::readyRead, s, [this, s, buf, parser, upgraded] {
      const QByteArray data = s->readAll();
      if (*upgraded) {
        parser->feed(data, [this](int op, const QByteArray& p) {
          if (op == 1) received << QString::fromUtf8(p);
        });
        return;
      }
      *buf += data;
      const int end = int(buf->indexOf("\r\n\r\n"));
      if (end < 0) return;
      const QList<QByteArray> lines = buf->left(end).split('\n');
      const QByteArray path = lines[0].split(' ').value(1);
      requests << QString::fromUtf8(path);
      QByteArray key;
      for (const QByteArray& l : lines)
        if (l.toLower().startsWith("sec-websocket-key:"))
          key = l.mid(18).trimmed();
      if (!key.isEmpty()) {
        *upgraded = true;
        const QByteArray accept =
            QCryptographicHash::hash(
                key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                QCryptographicHash::Sha1)
                .toBase64();
        s->write(
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Accept: " +
            accept + "\r\n\r\n");
        if (path.startsWith("/signalk/v1/stream")) {
          s->write(WebSocketClient::frame(
              1,
              R"({"name":"fake","version":"2.0","self":"vessels.urn:mrn:imo:mmsi:211000000"})",
              false));
          for (const QString& d : deltas)
            s->write(WebSocketClient::frame(1, d.toUtf8(), false));
        } else if (path.startsWith("/radar/")) {
          s->write(WebSocketClient::frame(2, radarFrame, false));
        }
        return;
      }
      QByteArray body = "{\"message\":\"not found\"}";
      QByteArray status = "404 Not Found";
      if (path == "/signalk/v2/api/vessels/self/radars") {
        status = "200 OK";
        body =
            QStringLiteral(
                R"({"radar-1":{"name":"HALO 24","brand":"Navico",)"
                R"("spokesPerRevolution":360,"maxSpokeLen":64,)"
                R"("streamUrl":"ws://127.0.0.1:%1/radar/1/stream",)"
                R"("legend":{"0":{"color":"#00000000"},"1":{"color":"#0000ffff"},)"
                R"("2":{"color":"#00ff00ff"},"3":{"color":"#ff0000ff"}}}})")
                .arg(port())
                .toUtf8();
      }
      s->write("HTTP/1.1 " + status +
               "\r\nContent-Type: application/json\r\nContent-Length: " +
               QByteArray::number(body.size()) +
               "\r\nConnection: close\r\n\r\n" + body);
      s->disconnectFromHost();
    });
  }
};

QString delta(const QString& path, const QString& valueJson) {
  return QStringLiteral(
             R"({"context":"vessels.urn:mrn:imo:mmsi:211000000","updates":[)"
             R"({"timestamp":"%1","values":[{"path":"%2","value":%3}]}]})")
      .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), path,
           valueJson);
}
}  // namespace

class SignalKTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("opencpn-qt-tests");
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
  }

  void frames() {
    // Short, 16 bit and 64 bit lengths, masked and unmasked, fragments.
    for (int len : {5, 300, 70000})
      for (bool mask : {false, true}) {
        QByteArray payload(len, 'x');
        payload[0] = 'a';
        const QByteArray f = WebSocketClient::frame(2, payload, mask);
        WebSocketClient::Parser parser;
        QByteArray got;
        int op = 0;
        // Byte by byte for the small one: partial frames wait.
        if (len == 5) {
          for (char c : f)
            QVERIFY(
                parser.feed(QByteArray(1, c), [&](int o, const QByteArray& p) {
                  op = o;
                  got = p;
                }));
        } else {
          QVERIFY(parser.feed(f, [&](int o, const QByteArray& p) {
            op = o;
            got = p;
          }));
        }
        QCOMPARE(op, 2);
        QCOMPARE(got, payload);
      }
    // A fragmented text message: "Hel" + "lo".
    QByteArray first = WebSocketClient::frame(1, "Hel", false);
    first[0] = char(0x01);  // FIN not set
    QByteArray cont = WebSocketClient::frame(0, "lo", false);
    WebSocketClient::Parser parser;
    QString text;
    QVERIFY(parser.feed(first + cont, [&](int o, const QByteArray& p) {
      if (o == 1) text = QString::fromUtf8(p);
    }));
    QCOMPARE(text, QStringLiteral("Hello"));
  }

  void deltasToNavStore() {
    auto& hub = SignalKHub::instance();
    hub.clear();
    NavUpdate u;
    QVERIFY(hub.navUpdate("navigation.speedOverGround", 3.0, u));
    QVERIFY(qAbs(*u.sog - 5.8315) < 0.001);
    QVERIFY(hub.navUpdate("navigation.headingTrue", M_PI / 2, u));
    QVERIFY(qAbs(*u.hdg - 90) < 1e-6);
    QVERIFY(hub.navUpdate("environment.depth.belowTransducer", 7.5, u));
    QCOMPARE(*u.depth, 7.5);
    QVERIFY(hub.navUpdate("environment.water.temperature", 290.15, u));
    QVERIFY(qAbs(*u.waterTempC - 17.0) < 1e-6);
    QVERIFY(hub.navUpdate("propulsion.main.revolutions", 30.0, u));
    QCOMPARE(*u.rpm, 1800.0);
    QVERIFY(!hub.navUpdate("electrical.batteries.house.voltage", 12.6, u));

    // The hello names the own vessel.
    hub.applyDelta(
        R"({"name":"x","self":"vessels.urn:mrn:imo:mmsi:211000000"})");
    // Applied deltas: position, wind as a pair, values in the hub.
    QCOMPARE(hub.applyDelta(delta("navigation.position",
                                  R"({"latitude":54.4,"longitude":10.2})")
                                .toUtf8()),
             1);
    QVERIFY(qAbs(NavStore::instance().ownShip().lat - 54.4) < 1e-9);
    hub.applyDelta(delta("environment.wind.angleApparent", "-0.5").toUtf8());
    hub.applyDelta(delta("environment.wind.speedApparent", "6.0").toUtf8());
    const Environment& env = NavStore::instance().environment();
    QVERIFY(qAbs(env.awa - (-28.648)) < 0.01);
    QVERIFY(qAbs(env.aws - 11.663) < 0.01);
    QVERIFY(hub.has("environment.wind.speedApparent"));
    // Other vessels are not the own ship.
    QCOMPARE(
        hub.applyDelta(
            R"({"context":"vessels.urn:mrn:imo:mmsi:999","updates":[{"values":[{"path":"navigation.speedOverGround","value":1}]}]})"),
        0);
  }

  void radarProtobuf() {
    RadarSpoke s;
    s.angle = 42;
    s.bearing = 100;
    s.rangeM = 1852;
    s.timeMs = 1760000000000;
    s.lat = 54.5;
    s.lon = 10.25;
    s.data = QByteArray("\x00\x01\x02\x0f", 4);
    const QByteArray msg = RadarProto::encode({s, s}, 7);
    QVector<RadarSpoke> back;
    int radar = 0;
    QVERIFY(RadarProto::decode(msg, back, &radar));
    QCOMPARE(radar, 7);
    QCOMPARE(back.size(), 2);
    QCOMPARE(back[0].angle, 42);
    QCOMPARE(back[0].bearing, 100);
    QCOMPARE(back[0].rangeM, 1852);
    QCOMPARE(back[0].timeMs, s.timeMs);
    QCOMPARE(back[0].lat, 54.5);
    QCOMPARE(back[0].data, s.data);
    // Truncated messages are rejected, unknown fields skipped.
    QVERIFY(!RadarProto::decode(msg.left(msg.size() - 3), back));
    QByteArray extra = msg;
    extra.append(char(9 << 3 | 0)).append(char(5));  // field 9 varint
    back.clear();
    QVERIFY(RadarProto::decode(extra, back));
    QCOMPARE(back.size(), 2);
  }

  void radarImage() {
    RadarImage img;
    img.reset(360, 100);
    // A target dead ahead at half range, one to starboard at 90°.
    for (int a = 0; a < 360; ++a) {
      RadarSpoke s;
      s.angle = a;
      s.bearing = (a + 30) % 360;  // heading 30°
      s.rangeM = 1000;
      s.data = QByteArray(50, 0);  // resampled to 100
      if (a == 0) s.data[25] = 15;
      if (a == 90) s.data[40] = 10;
      img.setSpoke(s);
    }
    QCOMPARE(img.rangeM(), 1000);
    QVERIFY(qAbs(img.headingDeg() - 30) < 1e-6);
    QCOMPARE(img.strongest(0, 1, 450, 550), 15);
    QCOMPARE(img.strongest(90, 1, 750, 850), 10);
    QCOMPARE(img.strongest(180, 10, 0, 1000), 0);
    // Rendered head up: the target above the center; north up turned by
    // the heading (30° to the right).
    RadarImage::Style st;
    QImage up = img.render(201, 0, 1000, st);
    QVERIFY(alphaNear(up, {100, 50}) > 0);
    QCOMPARE(alphaNear(up, {150, 100}), 0);
    QImage north = img.render(201, 30, 1000, st);
    const QPointF p(100 + 50 * std::sin(M_PI / 6),
                    100 - 50 * std::cos(M_PI / 6));
    QVERIFY(alphaNear(north, p.toPoint()) > 0);
    QCOMPARE(alphaNear(north, {100, 50}), 0);
    // A legend from the server colors the returns.
    st.legend = {qRgba(0, 0, 0, 0), qRgba(0, 0, 255, 255)};
    QImage leg = img.render(201, 0, 1000, st);
    int blue = 0;
    for (int y = 46; y <= 54; ++y)
      blue = std::max(blue, qBlue(leg.pixel(100, y)));
    QCOMPARE(blue, 255);
    // Threshold hides weak returns.
    st.legend.clear();
    st.threshold = 12;
    QImage thr = img.render(201, 0, 1000, st);
    QVERIFY(alphaNear(thr, {100, 50}) > 0);
    QCOMPARE(alphaNear(thr, {181, 100}), 0);
  }

  void radarSimulator() {
    RadarSimulator sim;
    sim.spokes = 720;
    sim.maxLen = 256;
    sim.rangeM = 2000;
    // Land: a square 1 km north of the ship.
    const double lat = 54.5, lon = 10.0;
    QPainterPath sq;
    auto m = [](double la, double lo) { return geo::toMercator(la, lo); };
    const double dLat = 1000.0 / 1852 / 60,
                 dLon = 500.0 / 1852 / 60 / std::cos(lat * M_PI / 180);
    sq.moveTo(m(lat + dLat, lon - dLon));
    sq.lineTo(m(lat + dLat, lon + dLon));
    sq.lineTo(m(lat + 2 * dLat, lon + dLon));
    sq.lineTo(m(lat + 2 * dLat, lon - dLon));
    sq.closeSubpath();
    auto polys = std::make_shared<QVector<MercatorPolygon>>();
    MercatorPolygon poly;
    poly.path = sq;
    poly.bounds = sq.boundingRect();
    *polys << poly;
    sim.land = polys;
    // A target 1.5 km to the east; heading north.
    double tLat, tLon;
    geo::destination(lat, lon, 90, 1500 / 1852.0, tLat, tLon);
    RadarImage img;
    img.reset(sim.spokes, sim.maxLen);
    for (const RadarSpoke& s :
         sim.spokesFor(0, sim.spokes, lat, lon, 0, {{tLat, tLon, 40}}))
      img.setSpoke(s);
    QVERIFY(img.strongest(0, 1, 950, 1100) >= 13);      // coastline ahead
    QVERIFY(img.strongest(0, 1, 750, 900) < 5);         // water before it
    QVERIFY(img.strongest(90, 1.5, 1400, 1600) >= 12);  // the target
    QVERIFY(img.strongest(270, 5, 800, 2000) < 5);      // open water west
    // Turned ship: the coast moves to port.
    img.reset(sim.spokes, sim.maxLen);
    for (const RadarSpoke& s : sim.spokesFor(0, sim.spokes, lat, lon, 90, {}))
      img.setSpoke(s);
    QVERIFY(img.strongest(270, 1, 950, 1100) >= 13);
  }

  void serverConnection() {
    FakeServer server;
    QVERIFY(server.isListening());
    server.deltas << delta("navigation.speedOverGround", "2.5")
                  << delta("environment.depth.belowTransducer", "12.3")
                  << delta("environment.water.temperature", "288.15");
    RadarSpoke s;
    s.angle = 0;
    s.rangeM = 926;
    s.data = QByteArray(64, char(3));
    server.radarFrame = RadarProto::encode({s});
    SignalKHub::instance().clear();

    ConnectionConfig cfg;
    cfg.type = ConnectionConfig::Type::SignalK;
    cfg.host = "127.0.0.1";
    cfg.port = server.port();
    QCOMPARE(cfg.typeName(), QStringLiteral("Signal K"));
    std::unique_ptr<Connection> c(Connection::create(cfg, nullptr));
    QVERIFY(c);
    c->start();
    QVERIFY(QTest::qWaitFor(
        [&] { return c->status() == Connection::Status::Connected; }, 5000));
    QVERIFY(QTest::qWaitFor(
        [] {
          return qAbs(NavStore::instance().environment().depth - 12.3) < 1e-6;
        },
        5000));
    QVERIFY(qAbs(NavStore::instance().ownShip().sog - 4.86) < 0.01);
    QVERIFY(qAbs(NavStore::instance().environment().waterTempC - 15) < 1e-6);
    QCOMPARE(SignalKHub::instance().selfContext(),
             QStringLiteral("vessels.urn:mrn:imo:mmsi:211000000"));

    // Radars of the server, with their legend.
    QVERIFY(QTest::qWaitFor(
        [] { return SignalKHub::instance().radars().size() == 1; }, 5000));
    const RadarInfo r = SignalKHub::instance().radars().first();
    QCOMPARE(r.id, QStringLiteral("radar-1"));
    QCOMPARE(r.spokes, 360);
    QCOMPARE(r.maxSpokeLen, 64);
    QCOMPARE(r.legend.size(), 4);
    QCOMPARE(qBlue(r.legend[1]), 255);
    QCOMPARE(qAlpha(r.legend[0]), 0);

    // The radar service streams its spokes while wanted.
    auto& radar = RadarService::instance();
    QObject viewer;
    radar.select("radar-1");
    QVERIFY(radar.sources().size() == 2);  // radar + simulator
    const int before = radar.spokeCount();
    radar.want(&viewer, true);
    QVERIFY(QTest::qWaitFor([&] { return radar.spokeCount() > before; }, 5000));
    QVERIFY(radar.receiving());
    QCOMPARE(radar.image().rangeM(), 926);
    QCOMPARE(int(radar.image().at(0, 10)), 3);
    radar.want(&viewer, false);
    radar.select("sim");

    // Sonar: echo samples on the configured path, and depth pings.
    auto& sonar = SonarService::instance();
    sonar.clear();
    SignalKHub::instance().applyDelta(
        delta(sonar.echoPath(), R"({"range":20,"samples":[0,0.2,1,0.5]})")
            .toUtf8());
    QCOMPARE(sonar.pings().size(), 1);
    QVERIFY(sonar.pings().last().hasEcho());
    QCOMPARE(sonar.pings().last().rangeM, 20.0f);
    QCOMPARE(int(uchar(sonar.pings().last().samples[2])), 255);
    QVERIFY(sonar.receivingEcho());
    QCOMPARE(sonar.source(), QStringLiteral("Signal K"));
    c->stop();
    QCOMPARE(c->status(), Connection::Status::Off);
    QVERIFY(SignalKHub::instance().radars().isEmpty());
  }

  void sonarPings() {
    auto& sonar = SonarService::instance();
    sonar.clear();
    float range;
    QByteArray samples;
    QVERIFY(SonarService::parseEcho(
        QJsonObject{{"range", 50}, {"samples", QJsonArray{0, 128, 255}}}, range,
        samples));
    QCOMPARE(range, 50.0f);
    QCOMPARE(samples.size(), 3);
    QCOMPARE(int(uchar(samples[1])), 128);
    QVERIFY(SonarService::parseEcho(
        QJsonObject{{"rangeM", 10},
                    {"data", QString(QByteArray("\x01\x02").toBase64())}},
        range, samples));
    QCOMPARE(samples.size(), 2);
    QVERIFY(!SonarService::parseEcho(QJsonObject{{"samples", QJsonArray{1}}},
                                     range, samples));
    // Depth readings (here NMEA) become pings once a second.
    NmeaParser parser;
    QObject::connect(&parser, &NmeaParser::navUpdate, &NavStore::instance(),
                     &NavStore::applyNavUpdate);
    QVERIFY(parser.parseLine(NmeaParser::makeSentence("SDDPT,8.4,0.0")));
    QVERIFY(parser.parseLine(NmeaParser::makeSentence("YXMTW,16.5,C")));
    QCOMPARE(NavStore::instance().environment().waterTempC, 16.5);
    QVERIFY(QTest::qWaitFor([&] { return sonar.pings().size() >= 1; }, 3000));
    QCOMPARE(sonar.pings().last().depthM, 8.4);
    QCOMPARE(sonar.pings().last().waterTempC, 16.5);
    QVERIFY(!sonar.pings().last().hasEcho());
    QVERIFY(parser.parseLine(NmeaParser::makeSentence("SDDPT,9.6,0.0")));
    QVERIFY(QTest::qWaitFor([&] { return sonar.pings().size() >= 2; }, 3000));
    double lo, hi;
    sonar.depthRange(600, lo, hi);
    QCOMPARE(lo, 8.4);
    QCOMPARE(hi, 9.6);
  }
};

QTEST_GUILESS_MAIN(SignalKTest)
#include "test_signalk.moc"
