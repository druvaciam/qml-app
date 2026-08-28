#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QIcon>
#include <QFile>
#include <QTextStream>

#include <cstdio>

static void customLog(QtMsgType, const QMessageLogContext &, const QString &msg) {
    std::fprintf(stderr, "%s\n", qPrintable(msg));
    std::fflush(stderr);
    QFile file(QStringLiteral("log.txt"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << msg << "\n";
    }
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(customLog);
    QGuiApplication app(argc, argv);

    // Set application icon for Windows taskbar and window instances
    QIcon appIcon(QStringLiteral(":/resources/app_icon.png"));
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral("resources/app_icon.png"));
    }
    app.setWindowIcon(appIcon);

    app.setApplicationName(QStringLiteral("QmlCommander"));
    app.setOrganizationName(QStringLiteral("QtCommanderTeam"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::warnings,
        [](const QList<QQmlError> &warnings) {
            for (const auto &w : warnings) {
                qWarning() << "QML error:" << w.toString();
            }
        }
    );

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [](const QUrl &url) {
            qWarning() << "Failed to create root QML object:" << url;
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection
    );

    engine.loadFromModule(QStringLiteral("QmlCommander"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        engine.load(QUrl(QStringLiteral("qrc:/QmlCommander/qml/Main.qml")));
    }

    return app.exec();
}
