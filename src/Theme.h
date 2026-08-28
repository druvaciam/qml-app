#pragma once

#include <QObject>
#include <QColor>
#include <QtQml/qqmlregistration.h>

class Theme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Backgrounds
    Q_PROPERTY(QColor bgApp READ bgApp CONSTANT)
    Q_PROPERTY(QColor bgPanel READ bgPanel CONSTANT)
    Q_PROPERTY(QColor bgPanelActive READ bgPanelActive CONSTANT)
    Q_PROPERTY(QColor bgHeader READ bgHeader CONSTANT)
    Q_PROPERTY(QColor bgRowAlt READ bgRowAlt CONSTANT)
    Q_PROPERTY(QColor bgHover READ bgHover CONSTANT)
    Q_PROPERTY(QColor bgSelected READ bgSelected CONSTANT)
    Q_PROPERTY(QColor bgSelectedHover READ bgSelectedHover CONSTANT)
    Q_PROPERTY(QColor bgDialog READ bgDialog CONSTANT)
    Q_PROPERTY(QColor bgInput READ bgInput CONSTANT)

    // Accents
    Q_PROPERTY(QColor accent READ accent CONSTANT)
    Q_PROPERTY(QColor accentHover READ accentHover CONSTANT)
    Q_PROPERTY(QColor accentActive READ accentActive CONSTANT)
    Q_PROPERTY(QColor success READ success CONSTANT)
    Q_PROPERTY(QColor warning READ warning CONSTANT)
    Q_PROPERTY(QColor danger READ danger CONSTANT)
    Q_PROPERTY(QColor dangerHover READ dangerHover CONSTANT)

    // Borders
    Q_PROPERTY(QColor borderSubtle READ borderSubtle CONSTANT)
    Q_PROPERTY(QColor borderActive READ borderActive CONSTANT)
    Q_PROPERTY(QColor borderFocus READ borderFocus CONSTANT)

    // Text Colors
    Q_PROPERTY(QColor textPrimary READ textPrimary CONSTANT)
    Q_PROPERTY(QColor textSecondary READ textSecondary CONSTANT)
    Q_PROPERTY(QColor textMuted READ textMuted CONSTANT)
    Q_PROPERTY(QColor textSelected READ textSelected CONSTANT)
    Q_PROPERTY(QColor textAccent READ textAccent CONSTANT)
    Q_PROPERTY(QColor textDanger READ textDanger CONSTANT)

    // File Type Colors
    Q_PROPERTY(QColor fileFolder READ fileFolder CONSTANT)
    Q_PROPERTY(QColor fileCode READ fileCode CONSTANT)
    Q_PROPERTY(QColor fileImage READ fileImage CONSTANT)
    Q_PROPERTY(QColor fileArchive READ fileArchive CONSTANT)
    Q_PROPERTY(QColor fileExec READ fileExec CONSTANT)
    Q_PROPERTY(QColor fileDoc READ fileDoc CONSTANT)
    Q_PROPERTY(QColor fileParent READ fileParent CONSTANT)
    Q_PROPERTY(QColor fileGeneric READ fileGeneric CONSTANT)

    // Typography
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)
    Q_PROPERTY(QString fontMono READ fontMono CONSTANT)
    Q_PROPERTY(int fontSizeSmall READ fontSizeSmall CONSTANT)
    Q_PROPERTY(int fontSizeBase READ fontSizeBase CONSTANT)
    Q_PROPERTY(int fontSizeMedium READ fontSizeMedium CONSTANT)
    Q_PROPERTY(int fontSizeLarge READ fontSizeLarge CONSTANT)
    Q_PROPERTY(int fontSizeTitle READ fontSizeTitle CONSTANT)

    // Dimensions
    Q_PROPERTY(int radiusSmall READ radiusSmall CONSTANT)
    Q_PROPERTY(int radiusBase READ radiusBase CONSTANT)
    Q_PROPERTY(int radiusLarge READ radiusLarge CONSTANT)
    Q_PROPERTY(int rowHeight READ rowHeight CONSTANT)
    Q_PROPERTY(int headerHeight READ headerHeight CONSTANT)
    Q_PROPERTY(int driveBarHeight READ driveBarHeight CONSTANT)
    Q_PROPERTY(int statusBarHeight READ statusBarHeight CONSTANT)
    Q_PROPERTY(int functionBarHeight READ functionBarHeight CONSTANT)

public:
    explicit Theme(QObject *parent = nullptr) : QObject(parent) {}

    // Backgrounds
    QColor bgApp() const { static const QColor c(QStringLiteral("#121418")); return c; }
    QColor bgPanel() const { static const QColor c(QStringLiteral("#1a1d24")); return c; }
    QColor bgPanelActive() const { static const QColor c(QStringLiteral("#1e222b")); return c; }
    QColor bgHeader() const { static const QColor c(QStringLiteral("#242933")); return c; }
    QColor bgRowAlt() const { static const QColor c(QStringLiteral("#16191f")); return c; }
    QColor bgHover() const { static const QColor c(QStringLiteral("#283040")); return c; }
    QColor bgSelected() const { static const QColor c(QStringLiteral("#1e3a5f")); return c; }
    QColor bgSelectedHover() const { static const QColor c(QStringLiteral("#254875")); return c; }
    QColor bgDialog() const { static const QColor c(QStringLiteral("#1f232c")); return c; }
    QColor bgInput() const { static const QColor c(QStringLiteral("#13161c")); return c; }

    // Accents
    QColor accent() const { static const QColor c(QStringLiteral("#38bdf8")); return c; }
    QColor accentHover() const { static const QColor c(QStringLiteral("#0ea5e9")); return c; }
    QColor accentActive() const { static const QColor c(QStringLiteral("#0284c7")); return c; }
    QColor success() const { static const QColor c(QStringLiteral("#4ade80")); return c; }
    QColor warning() const { static const QColor c(QStringLiteral("#fbbf24")); return c; }
    QColor danger() const { static const QColor c(QStringLiteral("#f87171")); return c; }
    QColor dangerHover() const { static const QColor c(QStringLiteral("#ef4444")); return c; }

    // Borders
    QColor borderSubtle() const { static const QColor c(QStringLiteral("#2d3342")); return c; }
    QColor borderActive() const { static const QColor c(QStringLiteral("#38bdf8")); return c; }
    QColor borderFocus() const { static const QColor c(QStringLiteral("#60a5fa")); return c; }

    // Text Colors
    QColor textPrimary() const { static const QColor c(QStringLiteral("#f3f4f6")); return c; }
    QColor textSecondary() const { static const QColor c(QStringLiteral("#9ca3af")); return c; }
    QColor textMuted() const { static const QColor c(QStringLiteral("#6b7280")); return c; }
    QColor textSelected() const { static const QColor c(QStringLiteral("#ffffff")); return c; }
    QColor textAccent() const { static const QColor c(QStringLiteral("#38bdf8")); return c; }
    QColor textDanger() const { static const QColor c(QStringLiteral("#fca5a5")); return c; }

    // File Type Colors
    QColor fileFolder() const { static const QColor c(QStringLiteral("#fbbf24")); return c; }
    QColor fileCode() const { static const QColor c(QStringLiteral("#38bdf8")); return c; }
    QColor fileImage() const { static const QColor c(QStringLiteral("#c084fc")); return c; }
    QColor fileArchive() const { static const QColor c(QStringLiteral("#f472b6")); return c; }
    QColor fileExec() const { static const QColor c(QStringLiteral("#4ade80")); return c; }
    QColor fileDoc() const { static const QColor c(QStringLiteral("#60a5fa")); return c; }
    QColor fileParent() const { static const QColor c(QStringLiteral("#e2e8f0")); return c; }
    QColor fileGeneric() const { static const QColor c(QStringLiteral("#9ca3af")); return c; }

    // Typography
    QString fontFamily() const { return QStringLiteral("Segoe UI, -apple-system, BlinkMacSystemFont, Roboto, sans-serif"); }
    QString fontMono() const { return QStringLiteral("Cascadia Code, Consolas, Courier New, monospace"); }
    int fontSizeSmall() const { return 11; }
    int fontSizeBase() const { return 13; }
    int fontSizeMedium() const { return 14; }
    int fontSizeLarge() const { return 16; }
    int fontSizeTitle() const { return 18; }

    // Sizing & Spacing
    int radiusSmall() const { return 4; }
    int radiusBase() const { return 6; }
    int radiusLarge() const { return 8; }
    int rowHeight() const { return 28; }
    int headerHeight() const { return 34; }
    int driveBarHeight() const { return 36; }
    int statusBarHeight() const { return 28; }
    int functionBarHeight() const { return 44; }

    Q_INVOKABLE static QString getFileIcon(const QString &type, bool isDir, bool isParent)
    {
        if (isParent) return QStringLiteral("⮥");
        if (isDir) return QStringLiteral("📁");
        if (type == "code") return QStringLiteral("📄");
        if (type == "image") return QStringLiteral("🖼️");
        if (type == "archive") return QStringLiteral("📦");
        if (type == "executable") return QStringLiteral("⚡");
        if (type == "document") return QStringLiteral("📝");
        if (type == "audio") return QStringLiteral("🎵");
        if (type == "video") return QStringLiteral("🎬");
        return QStringLiteral("🗎");
    }
};
