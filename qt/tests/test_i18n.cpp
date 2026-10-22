// German user interface: the JSON translator and the main window in German.
#include <QtTest>

#include "core/Translation.h"
#include "ui/DrawerPages.h"
#include "ui/GlassPanel.h"
#include "ui/MainWindow.h"
#include "ui/Overlays.h"
#include "ui/SideDrawer.h"
#include "ui/Theme.h"

class I18nTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .removeRecursively();
    QVERIFY(m_tr.loadJson(QStringLiteral(OCPN_QT_I18N_DIR "/de.json")));
    QVERIFY(m_tr.size() > 800);
    QCoreApplication::installTranslator(&m_tr);
  }

  void translator() {
    QCOMPARE(m_tr.languageCode(), QStringLiteral("de"));
    // Any context: the same text has the same translation.
    QCOMPARE(QCoreApplication::translate("BottomDock", "Chart"),
             QStringLiteral("Karte"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "Settings"),
             QStringLiteral("Einstellungen"));
    // Placeholders stay.
    QVERIFY(QCoreApplication::translate("PassagePanel", "Departure %1")
                .contains("%1"));
    // Plurals: singular for one, plural otherwise; %n is filled in.
    const QString one =
        QCoreApplication::translate("PassagePanel", "%n tack(s)", nullptr, 1);
    const QString three =
        QCoreApplication::translate("PassagePanel", "%n tack(s)", nullptr, 3);
    QVERIFY2(one.startsWith("1 ") && one != three, qPrintable(one));
    QVERIFY2(three.startsWith("3 "), qPrintable(three));
    QVERIFY(three.contains("Wenden"));
    // Unknown texts stay English.
    QCOMPARE(QCoreApplication::translate("X", "No such text"),
             QStringLiteral("No such text"));
    QCOMPARE(Translation::effectiveLanguage("de"), QStringLiteral("de"));
    QCOMPARE(Translation::effectiveLanguage("en"), QStringLiteral("en"));
    QVERIFY(!Translation::available().isEmpty());
  }

  void mainWindowInGerman() {
    Theme::instance().apply();
    MainWindow win(std::make_shared<LandData>());
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QStringList labels;
    for (auto* b : win.findChild<BottomDock*>()->findChildren<DockButton*>())
      labels << b->text();
    for (const char* t : {"Einstellungen", "Routen", "Markierungen", "Gezeiten",
                          "Wetter", "Karte", "Boot"})
      QVERIFY2(labels.contains(t), qPrintable(labels.join(", ")));
    // The settings hub in German.
    win.drawer()->open(win.pageIndex("settings"));
    QTest::qWait(300);
    bool general = false;
    for (auto* l : win.findChildren<QLabel*>())
      if (l->isVisible() && l->text() == "Allgemein") general = true;
    QVERIFY(general);
    const QString shot = qEnvironmentVariable("OCPN_QT_TEST_GERMAN_SHOT");
    if (!shot.isEmpty()) {
      win.setView(MainWindow::View::Boat);
      QTest::qWait(300);
      win.grab().save(shot);
    }
  }

private:
  JsonTranslator m_tr;
};

QTEST_MAIN(I18nTest)
#include "test_i18n.moc"
