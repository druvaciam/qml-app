#include "DriveInfo.h"
#include <QDir>
#include <QFileInfo>
#include <QLocale>

DriveInfo::DriveInfo(const QStorageInfo &storage)
    : m_rootPath(storage.rootPath()),
      m_name(storage.name()),
      m_displayName(storage.displayName()),
      m_fileSystemType(QString::fromUtf8(storage.fileSystemType())),
      m_bytesTotal(storage.bytesTotal()),
      m_bytesFree(storage.bytesFree()),
      m_bytesAvailable(storage.bytesAvailable()),
      m_isReady(storage.isReady() && storage.isValid())
{
    m_formattedTotal = formatBytes(m_bytesTotal);
    m_formattedFree = formatBytes(m_bytesAvailable);

    if (m_bytesTotal > 0) {
        m_percentUsed = static_cast<double>(m_bytesTotal - m_bytesAvailable) / static_cast<double>(m_bytesTotal);
    } else {
        m_percentUsed = 0.0;
    }

    if (m_displayName.isEmpty()) {
        m_displayName = m_rootPath;
    }
}

QString DriveInfo::formatBytes(qint64 bytes)
{
    if (bytes < 0) return QStringLiteral("0 B");
    
    constexpr double KB = 1024.0;
    constexpr double MB = 1024.0 * KB;
    constexpr double GB = 1024.0 * MB;
    constexpr double TB = 1024.0 * GB;

    double dBytes = static_cast<double>(bytes);
    if (dBytes >= TB) {
        return QString::asprintf("%.2f TB", dBytes / TB);
    } else if (dBytes >= GB) {
        return QString::asprintf("%.2f GB", dBytes / GB);
    } else if (dBytes >= MB) {
        return QString::asprintf("%.1f MB", dBytes / MB);
    } else if (dBytes >= KB) {
        return QString::asprintf("%.1f KB", dBytes / KB);
    } else {
        return QString::asprintf("%lld B", bytes);
    }
}

QList<DriveInfo> DriveInfo::getMountedDrives()
{
    QList<DriveInfo> list;
    const auto mountedVolumes = QStorageInfo::mountedVolumes();
    for (const auto &storage : mountedVolumes) {
        if (storage.isValid() && storage.isReady()) {
            list.append(DriveInfo(storage));
        }
    }

    // Fallback on Windows if storage info is empty or missing drives
    if (list.isEmpty()) {
        const auto drives = QDir::drives();
        for (const auto &drive : drives) {
            QStorageInfo storage(drive.absoluteFilePath());
            list.append(DriveInfo(storage));
        }
    }

    return list;
}
