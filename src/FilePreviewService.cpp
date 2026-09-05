#include "FilePreviewService.h"
#include "DriveInfo.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QProcess>
#include <QSaveFile>
#include <QUrl>
#include <QStringConverter>
#include <QStringDecoder>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

FilePreviewService::FilePreviewService(QObject *parent)
    : QObject(parent)
{
}

bool FilePreviewService::mediaSupported()
{
#ifdef QMLCOMMANDER_HAS_MULTIMEDIA
    return true;
#else
    return false;
#endif
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

    // Recognised by MIME type first, with an extension list as the fallback -
    // a MIME database can be incomplete, and a file with no extension can still
    // be identified by its contents.
    const bool isAudio = mime.name().startsWith(QStringLiteral("audio/")) ||
                         ext == "mp3" || ext == "wav" || ext == "flac" || ext == "ogg" ||
                         ext == "m4a" || ext == "aac" || ext == "wma" || ext == "opus";
    const bool isVideo = mime.name().startsWith(QStringLiteral("video/")) ||
                         ext == "mp4" || ext == "mkv" || ext == "avi" || ext == "mov" ||
                         ext == "webm" || ext == "wmv" || ext == "m4v" || ext == "mpg" ||
                         ext == "mpeg";

    result["isImage"] = isImage;
    result["isAudio"] = isAudio && !isImage;
    result["isVideo"] = isVideo && !isImage;

    if (isAudio || isVideo) {
        // Nothing is read here. The player is handed the path and streams it
        // itself, which is the only sane way to open a two hour video.
        result["isText"] = false;
        result["content"] = QString();
        result["fileUrl"] = QUrl::fromLocalFile(filePath).toString();
        return result;
    }

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
    QByteArray data;
    bool opened = false;
    bool isTruncated = false;
    QString openError;

#ifdef Q_OS_WIN
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        data = file.read(maxBytes);
        isTruncated = (file.size() > maxBytes);
        file.close();
        opened = true;
    } else {
        openError = file.errorString();
        // Fallback: Windows CreateFile with full sharing flags (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
        HANDLE hFile = CreateFileW(
            reinterpret_cast<LPCWSTR>(filePath.utf16()),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER fileSize;
            if (GetFileSizeEx(hFile, &fileSize)) {
                DWORD toRead = static_cast<DWORD>(std::min<qint64>(maxBytes, fileSize.QuadPart));
                data.resize(toRead);
                DWORD bytesRead = 0;
                if (ReadFile(hFile, data.data(), toRead, &bytesRead, nullptr)) {
                    data.resize(bytesRead);
                    isTruncated = (fileSize.QuadPart > maxBytes);
                    opened = true;
                }
            }
            CloseHandle(hFile);
        } else {
            DWORD winErr = GetLastError();
            if (winErr == ERROR_ACCESS_DENIED) {
                openError = QStringLiteral("Access is denied (system protected or administrator permissions required).");
            } else if (winErr == ERROR_SHARING_VIOLATION) {
                openError = QStringLiteral("Sharing violation (file is in exclusive use by another process).");
            }
        }
    }
#else
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        data = file.read(maxBytes);
        isTruncated = (file.size() > maxBytes);
        file.close();
        opened = true;
    } else {
        openError = file.errorString();
    }
#endif

    if (!opened) {
        result["isText"] = false;
        result["isImage"] = false;
        result["error"] = openError.isEmpty() ? QStringLiteral("Cannot open file for reading.") : openError;
        return result;
    }

    // Check for UTF-16 BOMs
    QString textContent;
    bool isBinary = false;

    if (data.size() >= 2) {
        quint8 b0 = static_cast<quint8>(data.at(0));
        quint8 b1 = static_cast<quint8>(data.at(1));
        if (b0 == 0xff && b1 == 0xfe) {
            // UTF-16 LE
            textContent = QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData() + 2), (data.size() - 2) / 2);
        } else if (b0 == 0xfe && b1 == 0xff) {
            // UTF-16 BE
            QStringDecoder decoder(QStringDecoder::Utf16BE);
            textContent = decoder(data.mid(2));
        }
    }

    if (textContent.isEmpty() && !data.isEmpty()) {
        // Check for binary bytes (null bytes)
        int checkLen = std::min<int>(data.size(), 1024);
        int nullCount = 0;
        for (int i = 0; i < checkLen; ++i) {
            char c = data.at(i);
            if (c == 0) {
                nullCount++;
            }
        }

        if (nullCount > 0) {
            isBinary = true;
        } else {
            // Decode as UTF-8 with fallback to Latin-1
            auto decoder = QStringDecoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
            textContent = decoder(data);
            if (decoder.hasError()) {
                textContent = QString::fromLatin1(data);
            }
        }
    }

    if (isBinary) {
        result["isText"] = false;
        result["isBinary"] = true;
        result["content"] = QStringLiteral("[Binary File - Preview Not Available]");
    } else {
        result["isText"] = true;
        result["isBinary"] = false;
        result["content"] = textContent;
        result["isTruncated"] = isTruncated;
        result["lineCount"] = textContent.count(QLatin1Char('\n')) + 1;
    }

    return result;
}

bool FilePreviewService::saveTextFile(const QString &filePath, const QString &content)
{
    // QSaveFile writes to a temporary file and renames on commit(), so a failed
    // or partial write leaves the original file untouched.
    // No QIODevice::Text: the content was read as raw bytes and must be written
    // back unchanged, otherwise existing CRLF pairs get a second CR each.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    const QByteArray utf8 = content.toUtf8();
    if (file.write(utf8) != utf8.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        return false;
    }

    emit fileSaved(filePath);
    return true;
}

bool FilePreviewService::openInDefaultApp(const QString &filePath)
{
#ifndef Q_OS_WIN
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    if (ext == "txt" || ext == "log" || ext == "ini" || ext == "conf" || ext == "json" ||
        ext == "cpp" || ext == "h" || ext == "qml" || ext == "md" || ext == "xml" || ext == "sh") {
        if (QProcess::startDetached(QStringLiteral("mousepad"), {filePath})) {
            return true;
        }
    }
#endif
    return QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

bool FilePreviewService::openInTerminal(const QString &dirPath)
{
    QString path = dirPath;
    if (path.isEmpty()) {
        path = QDir::currentPath();
    }

#if defined(Q_OS_WIN)
    // A folder name may legally contain a single quote, which would otherwise close
    // the quoted argument and let the rest of the name run as PowerShell code.
    // PowerShell escapes a literal quote inside a single-quoted string by doubling it.
    QString quoted = path;
    quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QProcess::startDetached(QStringLiteral("powershell.exe"), {QStringLiteral("-NoExit"), QStringLiteral("-Command"), QStringLiteral("Set-Location '%1'").arg(quoted)});
#elif defined(Q_OS_MAC)
    return QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-a"), QStringLiteral("Terminal"), path});
#else
    // Pass the directory as the working directory rather than interpolating it
    // into a shell command string.
    if (QProcess::startDetached(QStringLiteral("xterm"), {QStringLiteral("-e"), QStringLiteral("bash")}, path)) {
        return true;
    }
    return QProcess::startDetached(QStringLiteral("xdg-open"), {path});
#endif
}
