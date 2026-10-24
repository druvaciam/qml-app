#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;
class QTranslator;

/**
 * The language the interface is in, switchable while the program runs.
 *
 * Every piece of visible text in the QML goes through qsTr(), which the engine
 * treats as a binding. Calling QQmlEngine::retranslate() re-runs all of them,
 * so a new language appears without restarting. C++ messages use tr() and are
 * built when they are needed, so they come out in whatever language is current
 * at that moment.
 */
class Localization : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// "en" or "be". Writing it switches the interface.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    /// What to put on the button: "EN", "БЕ".
    Q_PROPERTY(QString languageLabel READ languageLabel NOTIFY languageChanged)
    /// The language's own name for itself, for a tooltip or a menu.
    Q_PROPERTY(QString languageName READ languageName NOTIFY languageChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)

public:
    /// The one instance, owned by the application. main() asks for it before
    /// the engine loads any QML, so the saved language is already installed
    /// when the first window is built.
    static Localization *instance();
    /// What QML_SINGLETON calls. Hands back the same instance rather than
    /// letting the engine construct a second one.
    static Localization *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    QString language() const { return m_language; }
    QString languageLabel() const;
    QString languageName() const;
    static QStringList availableLanguages() { return {QStringLiteral("en"), QStringLiteral("be")}; }

    Q_INVOKABLE void setLanguage(const QString &code);
    /// Steps to the next language in availableLanguages(), for the button and
    /// the keyboard shortcut.
    Q_INVOKABLE void nextLanguage();

signals:
    void languageChanged();

private:
    explicit Localization(QObject *parent = nullptr);

    /// What retranslate() is asked of. Without an engine a switch would change
    /// only the strings built from now on, leaving the window as it was.
    QQmlEngine *m_engine = nullptr;
    /// Installed in the application; replaced, never stacked, so the previous
    /// language cannot answer for a string the new one does not have.
    QTranslator *m_translator = nullptr;
    QString m_language;
};
