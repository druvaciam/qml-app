#include "FilePreviewService.h"
#include "DriveInfo.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QProcess>
#include <QUrl>

FilePreviewService::FilePreviewService(QObject *parent)
    : QObject(parent)
{
}

QVariantMap FilePreviewService::loadPreview(const QString &filePath, int maxBytes)
{
    QVariantMap result;
    QFileInfo info(filePath);

    if (!info.exists()) {
        result["error"] = QStringLiteral("File does not exist.");
        result["isValid"] = false;
        return result;
    }

    result["fileName"] = info.fileName();
    result["filePath"] = info.absoluteFilePath();
    result["fileSize"] = info.size();
    result["formattedSize"] = DriveInfo::formatBytes(info.size());
    result["lastModified"] = info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    result["isValid"] = true;

    QMimeDatabase mimeDb;
    QMimeType mime = mimeDb.mimeTypeForFile(info);
    result["mimeType"] = mime.name();

    QString ext = info.suffix().toLower();
    bool isImage = mime.name().startsWith(QStringLiteral("image/")) ||
                   ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
                   ext == "bmp" || ext == "svg" || ext == "webp" || ext == "ico";

    result["isImage"] = isImage;

    if (isImage) {
        QImageReader reader(filePath);
        QSize size = reader.size();
        if (size.isValid()) {
            result["imageWidth"] = size.width();
            result["imageHeight"] = size.height();
        } else {
            result["imageWidth"] = 0;
            result["imageHeight"] = 0;
        }
        result["isText"] = false;
        result["content"] = QString();
        result["fileUrl"] = QUrl::fromLocalFile(filePath).toString();
        return result;
    }

    // Attempt text reading
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result["isText"] = false;
        result["error"] = QStringLiteral("Cannot open file for reading.");
        return result;
    }

    QByteArray data = file.read(maxBytes);
    bool isTruncated = (file.size() > maxBytes);
    file.close();

    // Check if binary characters are present
    bool isBinary = false;
    for (int i = 0; i < std::min<int>(data.size(), 1024); ++i) {
        char c = data.at(i);
        if (c == 0 || (static_cast<unsigned char>(c) < 32 && c != '\t' && c != '\r' && c != '\n')) {
            isBinary = true;
            break;
        }
    }

    if (isBinary) {
        result["isText"] = false;
        result["isBinary"] = true;
        result["content"] = QStringLiteral("[Binary File - Preview Not Available]");
    } else {
        result["isText"] = true;
        result["isBinary"] = false;
        QString textContent = QString::fromUtf8(data);
        result["content"] = textContent;
        result["isTruncated"] = isTruncated;
        result["lineCount"] = textContent.count(QLatin1Char('\n')) + 1;
    }

    return result;
}

bool FilePreviewService::saveTextFile(const QString &filePath, const QString &content)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QByteArray utf8 = content.toUtf8();
    qint64 written = file.write(utf8);
    file.close();
    return (written == utf8.size());
}

bool FilePreviewService::openInDefaultApp(const QString &filePath)
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

bool FilePreviewService::openInTerminal(const QString &dirPath)
{
    QString path = dirPath;
    if (path.isEmpty()) {
        path = QDir::currentPath();
    }

#if defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("powershell.exe"), {QStringLiteral("-NoExit"), QStringLiteral("-Command"), QString("Set-Location '%1'").arg(path)});
#elif defined(Q_OS_MAC)
    return QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-a"), QStringLiteral("Terminal"), path});
#else
    return QProcess::startDetached(QStringLiteral("xdg-open"), {path});
#endif
}
