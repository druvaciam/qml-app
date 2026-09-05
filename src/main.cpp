#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QIcon>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QStandardPaths>
#include <QThread>
#include <QTextStream>

#include <cstdio>

#include "Logging.h"

namespace {

/// Debug and Release write to separate files, so a log is never a mixture of
/// two builds - which is exactly the confusion that made a stale binary look
/// like a live bug earlier in this project's history.
constexpr const char *kBuildTag =
#ifdef QT_DEBUG
    "debug";
#else
    "release";
#endif

/// Files older than this are removed on startup. Rotation is by day rather than
/// by size: a dated file is easy to hand to someone ("the one from Tuesday"),
/// and a size-based rotation throws away the beginning of the session, which is
/// usually the part worth reading.
constexpr int kKeepDays = 7;

QString logDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : base + QStringLiteral("/logs");
}

/// qmlcommander-debug-2026-09-01.log
QString logFileName(const QDate &date)
{
    return QStringLiteral("qmlcommander-%1-%2.log")
        .arg(QString::fromLatin1(kBuildTag), date.toString(QStringLiteral("yyyy-MM-dd")));
}

void removeExpiredLogs(const QString &dir)
{
    const QDate cutoff = QDate::currentDate().addDays(-kKeepDays);
    const QFileInfoList old =
        QDir(dir).entryInfoList(QStringList() << QStringLiteral("qmlcommander-*.log"), QDir::Files);
    for (const QFileInfo &info : old) {
        if (info.lastModified().date() < cutoff) {
            QFile::remove(info.absoluteFilePath());
        }
    }
}

/// A short, stable number per thread. The GUI thread is always 1, which is the
/// distinction that matters most here: every other number is a pool thread
/// reading a folder, and seeing which is which is half the value of the log.
int threadNumber()
{
    if (QCoreApplication::instance()
        && QThread::currentThread() == QCoreApplication::instance()->thread()) {
        return 1;
    }
    static QAtomicInt next{2};
    thread_local const int mine = next.fetchAndAddOrdered(1);
    return mine;
}

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "INFO ";
}

/// One line per message, in the shape a log viewer expects:
///
///     2026-09-01 14:23:45.123|DEBUG| 2|qmlcommander.model|read /big in 122 ms
///
/// Timestamp, level, thread, logger name, message - the same fields NLog writes
/// by default, in the same order, so the file is greppable by any of them.
void writeMessage(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const QString category = QString::fromLatin1(context.category ? context.category : "default");
    const QString line = QStringLiteral("%1|%2|%3|%4|%5")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                  QString::fromLatin1(levelName(type)),
                                  QString::number(threadNumber()).rightJustified(2),
                                  category,
                                  msg);

    std::fprintf(stderr, "%s\n", qPrintable(line));
    std::fflush(stderr);

    // The folder read runs on a worker thread, so two threads can arrive here at
    // the same moment and interleave halfway through a line.
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    // Opened once rather than per message, on first use.
    static QFile *logFile = []() -> QFile * {
        const QString dir = logDirectory();
        if (dir.isEmpty() || !QDir().mkpath(dir)) {
            return nullptr;
        }
        removeExpiredLogs(dir);

        auto *f = new QFile(dir + QLatin1Char('/') + logFileName(QDate::currentDate()));
        if (!f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            delete f;
            return nullptr;
        }
        // The file is appended to across runs, so mark where this one starts.
        // Without it there is no telling a live error from yesterday's.
        QTextStream header(f);
        header << "\n===== session started "
               << QDateTime::currentDateTime().toString(Qt::ISODate)
               << " (" << kBuildTag << " build) =====\n";
        header.flush();
        return f;
    }();

    if (logFile) {
        QTextStream out(logFile);
        out << line << "\n";
        out.flush();
    }
}

/// Must run after the application and organisation names are set: the log
/// directory is derived from them.
void installLogging()
{
    qInstallMessageHandler(writeMessage);

    // Debug detail is on in a debug build and off in a release one. Either can
    // be overridden at run time without rebuilding, per area:
    //   set QT_LOGGING_RULES=qmlcommander.ops.debug=true
    // The #ifdef is deliberately outside the macro call. A preprocessor
    // directive inside a macro argument list is undefined behaviour: g++ let it
    // pass, MSVC rejects it outright with "invalid character: '#'".
    // "qml" is Qt's own category for console.log in QML. Setting any rules at
    // all replaces the defaults, so without naming it here a developer's
    // console.log vanishes from the log while console.warn still arrives -
    // warnings are not debug level, so they were never filtered.
#ifdef QT_DEBUG
    constexpr const char *kDefaultRules = "qmlcommander.*.debug=true\n"
                                          "qml.debug=true";
#else
    constexpr const char *kDefaultRules = "qmlcommander.*.debug=false\n"
                                          "qml.debug=false";
#endif
    QLoggingCategory::setFilterRules(QString::fromLatin1(kDefaultRules));

    qCInfo(lcApp).noquote() << "QmlCommander" << QCoreApplication::applicationVersion()
                            << QStringLiteral("(%1 build)").arg(QString::fromLatin1(kBuildTag));
    qCInfo(lcApp).noquote() << "logging to" << logDirectory() + QLatin1Char('/')
                                                   + logFileName(QDate::currentDate());
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    app.setApplicationName(QStringLiteral("QmlCommander"));
    app.setOrganizationName(QStringLiteral("QtCommanderTeam"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    installLogging();

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
                qCWarning(lcApp).noquote() << "QML:" << w.toString();
            }
        }
    );

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [](const QUrl &url) {
            qCCritical(lcApp).noquote() << "failed to create the root QML object:" << url.toString();
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection
    );

    engine.loadFromModule(QStringLiteral("QmlCommander"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        qCWarning(lcApp) << "module load produced no root object, falling back to the resource path";
        engine.load(QUrl(QStringLiteral("qrc:/QmlCommander/qml/Main.qml")));
    }

    qCInfo(lcApp) << "startup complete, entering the event loop";
    const int code = app.exec();
    qCInfo(lcApp) << "exiting with code" << code;
    return code;
}
