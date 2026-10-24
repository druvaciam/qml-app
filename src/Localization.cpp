#include "Localization.h"
#include "Logging.h"

#include <QCoreApplication>
#include <QJSEngine>
#include <QQmlEngine>
#include <QSettings>
#include <QTranslator>

Localization::Localization(QObject *parent)
    : QObject(parent)
{
    const QString saved = QSettings().value(QStringLiteral("session/language"),
                                            QStringLiteral("en")).toString();
    if (saved != QStringLiteral("en")) {
        setLanguage(saved);
    } else {
        m_language = QStringLiteral("en");
    }
}

Localization *Localization::instance()
{
    static Localization *self = new Localization(QCoreApplication::instance());
    return self;
}

Localization *Localization::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(jsEngine);
    Localization *self = instance();
    self->m_engine = qmlEngine;
    // The engine would otherwise delete a singleton it believes it owns; this
    // one belongs to the application and outlives the engine.
    QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    return self;
}

QString Localization::languageLabel() const
{
    return m_language == QStringLiteral("be") ? QStringLiteral("БЕ") : QStringLiteral("EN");
}

QString Localization::languageName() const
{
    return m_language == QStringLiteral("be") ? QStringLiteral("Беларуская") : QStringLiteral("English");
}

void Localization::setLanguage(const QString &code)
{
    const QString wanted = availableLanguages().contains(code) ? code : QStringLiteral("en");
    if (wanted == m_language) {
        return;
    }

    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }

    // English is the language the strings are written in, so it needs no
    // translator at all - removing the other one is the whole switch.
    if (wanted != QStringLiteral("en")) {
        auto *loaded = new QTranslator(this);
        const QString file = QStringLiteral(":/i18n/qmlcommander_%1.qm").arg(wanted);
        // isEmpty() as well as load(): a catalogue built from a missing or
        // empty .ts loads perfectly and translates nothing, which looks like
        // a switch that did not happen rather than a build that went wrong.
        if (!loaded->load(file) || loaded->isEmpty()) {
            const bool empty = !loaded->isEmpty() ? false : true;
            delete loaded;
            qCWarning(lcApp) << (empty ? "empty translation file" : "no translation file")
                             << file << "- staying in English";
            m_language = QStringLiteral("en");
            QSettings().setValue(QStringLiteral("session/language"), m_language);
            emit languageChanged();
            return;
        }
        QCoreApplication::installTranslator(loaded);
        m_translator = loaded;
    }

    m_language = wanted;
    QSettings().setValue(QStringLiteral("session/language"), m_language);

    // Re-runs every qsTr() binding in the loaded QML. Without this the new
    // translator only affects text built from now on.
    if (m_engine) {
        m_engine->retranslate();
    }

    qCInfo(lcApp).noquote() << "language ->" << m_language;
    emit languageChanged();
}

void Localization::nextLanguage()
{
    const QStringList all = availableLanguages();
    const int at = all.indexOf(m_language);
    setLanguage(all.at((at + 1) % all.size()));
}
