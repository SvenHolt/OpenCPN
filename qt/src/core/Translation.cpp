#include "core/Translation.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>

bool JsonTranslator::loadJson(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  m_map.clear();
  for (auto it = o.begin(); it != o.end(); ++it) {
    QStringList forms;
    if (it->isArray())
      for (const auto& v : it->toArray()) forms << v.toString();
    else
      forms << it->toString();
    if (!forms.isEmpty() && !forms.first().isEmpty())
      m_map.insert(it.key(), forms);
  }
  m_language = QFileInfo(path).completeBaseName();
  return !m_map.isEmpty();
}

QString JsonTranslator::translate(const char*, const char* sourceText,
                                  const char*, int n) const {
  if (!sourceText) return {};
  const auto it = m_map.constFind(QString::fromUtf8(sourceText));
  if (it == m_map.constEnd()) return {};
  // German: singular for exactly one, plural otherwise.
  if (it->size() > 1) return n == 1 ? it->at(0) : it->at(1);
  return it->first();
}

QString Translation::effectiveLanguage(const QString& setting) {
  if (setting == "de" || setting == "en") return setting;
  return QLocale::system().language() == QLocale::German ? "de" : "en";
}

QString Translation::install(const QString& setting) {
  const QString lang = effectiveLanguage(setting);
  if (lang == "en") {
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    return lang;
  }
  auto* app = QCoreApplication::instance();
  auto* own = new JsonTranslator(app);
  if (!own->loadJson(":/i18n/" + lang + ".json")) {
    delete own;
    return "en";
  }
  QCoreApplication::installTranslator(own);
  // Qt's own texts (standard dialogs, context menus).
  auto* qt = new QTranslator(app);
  if (qt->load("qtbase_" + lang,
               QLibraryInfo::path(QLibraryInfo::TranslationsPath)) ||
      qt->load("qtbase_" + lang,
               QCoreApplication::applicationDirPath() + "/translations"))
    QCoreApplication::installTranslator(qt);
  else
    delete qt;
  QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
  return lang;
}

QList<QPair<QString, QString>> Translation::available() {
  return {{"system", QString()}, {"en", "English"}, {"de", "Deutsch"}};
}
