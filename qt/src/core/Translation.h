// User interface languages. The German texts live in i18n/de.json (English
// source text → translation; [singular, plural] for texts with %n) and are
// installed as a QTranslator before any widget or store is created.
#pragma once

#include <QHash>
#include <QStringList>
#include <QTranslator>

/** A translator from a JSON dictionary; the context is not needed because
 *  the same English text has the same translation everywhere. */
class JsonTranslator : public QTranslator {
  Q_OBJECT
public:
  using QTranslator::QTranslator;
  bool loadJson(const QString& path);
  bool isEmpty() const override { return m_map.isEmpty(); }
  QString languageCode() const { return m_language; }
  QString translate(const char* context, const char* sourceText,
                    const char* disambiguation = nullptr,
                    int n = -1) const override;
  int size() const { return int(m_map.size()); }

private:
  QHash<QString, QStringList> m_map;
  QString m_language;
};

namespace Translation {
/** "de" or "en" for a setting ("system", "de", "en"). */
QString effectiveLanguage(const QString& setting);
/** Installs the translators for the language (app and Qt's own dialogs)
 *  and sets the default locale; English needs none. Returns the
 *  language in use. */
QString install(const QString& setting);
/** Languages offered in the settings: code and native name. */
QList<QPair<QString, QString>> available();
}  // namespace Translation
