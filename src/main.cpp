#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QIcon>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

// The log lives in the user's app data directory, not next to the working
// directory, which may be read-only or somewhere unexpected. The file is opened
// once rather than per message, and the handler is called from worker threads
// so writes are serialised.
static void customLog(QtMsgType, const QMessageLogContext &, const QString &msg) {
    std::fprintf(stderr, "%s\n", qPrintable(msg));
    std::fflush(stderr);

    static QMutex mutex;
    QMutexLocker locker(&mutex);

    static QFile *logFile = []() -> QFile * {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (dir.isEmpty() || !QDir().mkpath(dir)) {
            return nullptr;
        }
        const QString path = dir + QStringLiteral("/log.txt");
        // Start a new file once the old one passes 1 MB instead of growing forever.
        if (QFileInfo(path).size() > 1024 * 1024) {
            QFile::remove(path);
        }
        auto *f = new QFile(path);
        if (!f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            delete f;
            return nullptr;
        }
        return f;
    }();

    if (logFile) {
        QTextStream out(logFile);
        out << msg << "\n";
        out.flush();
    }
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    app.setApplicationName(QStringLiteral("QmlCommander"));
    app.setOrganizationName(QStringLiteral("QtCommanderTeam"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    // Installed after the application name is set: the log path is derived from it.
    qInstallMessageHandler(customLog);

    // Set application icon for Windows taskbar and window instances
    QIcon appIcon(QStringLiteral(":/resources/app_icon.png"));
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral("resources/app_icon.png"));
    }
    app.setWindowIcon(appIcon);

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
