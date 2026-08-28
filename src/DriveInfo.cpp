#include "DriveInfo.h"
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSet>

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

    if (m_displayName.isEmpty() || m_displayName == m_rootPath) {
#ifdef Q_OS_WIN
        m_displayName = m_rootPath;
#else
        if (m_rootPath == QStringLiteral("/")) {
            m_displayName = QStringLiteral("/");
        } else if (m_rootPath.startsWith(QStringLiteral("/mnt/")) && m_rootPath.length() == 6) {
            // WSL Windows drives: /mnt/c -> C:
            QChar driveLetter = m_rootPath.at(5).toUpper();
            m_displayName = QString(QStringLiteral("%1:")).arg(driveLetter);
        } else if (m_rootPath.startsWith(QStringLiteral("/media/")) || m_rootPath.startsWith(QStringLiteral("/run/media/"))) {
            m_displayName = QFileInfo(m_rootPath).fileName();
            if (m_displayName.isEmpty()) m_displayName = m_rootPath;
        } else {
            m_displayName = m_rootPath;
        }
#endif
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

    QSet<QString> seenDevices;
    QSet<QString> seenRoots;

    for (const auto &storage : mountedVolumes) {
        if (!storage.isValid() || !storage.isReady()) {
            continue;
        }

        QString root = storage.rootPath();
        QByteArray fsType = storage.fileSystemType().toLower();
        QString device = QString::fromUtf8(storage.device());

#ifndef Q_OS_WIN
        // 1. Skip single file mounts (e.g. Docker binds of /etc/resolv.conf, /etc/hosts)
        QFileInfo fi(root);
        if (!fi.isDir()) {
            continue;
        }

        // 2. Skip pseudo / virtual filesystems
        static const QSet<QByteArray> ignoredFs = {
            "proc", "sysfs", "devtmpfs", "devpts", "cgroup", "cgroup2",
            "pstore", "bpf", "tracefs", "debugfs", "securityfs", "configfs",
            "fusectl", "mqueue", "hugetlbfs", "autofs", "ramfs", "squashfs",
            "nsfs", "overlayfs"
        };
        if (ignoredFs.contains(fsType)) {
            continue;
        }

        // 3. Skip internal system paths not meant as user drives
        if (root.startsWith(QStringLiteral("/proc")) ||
            root.startsWith(QStringLiteral("/sys")) ||
            root.startsWith(QStringLiteral("/dev")) ||
            root.startsWith(QStringLiteral("/etc")) ||
            root.startsWith(QStringLiteral("/tmp")) ||
            root.startsWith(QStringLiteral("/var")) ||
            root.startsWith(QStringLiteral("/mnt/wsl")) ||   // WSL & WSLg internal communication sockets
            (root.startsWith(QStringLiteral("/run")) && !root.startsWith(QStringLiteral("/run/media")))) {
            continue;
        }

        // 4. Skip duplicate submounts of the same underlying block device (unless root is "/")
        if (root != QStringLiteral("/") && !device.isEmpty()) {
            if (seenDevices.contains(device)) {
                continue;
            }
        }
#endif

        if (seenRoots.contains(root)) {
            continue;
        }

        seenRoots.insert(root);
        if (!device.isEmpty()) {
            seenDevices.insert(device);
        }

        list.append(DriveInfo(storage));
    }

#ifndef Q_OS_WIN
    // Ensure root "/" is always present on Linux
    if (!seenRoots.contains(QStringLiteral("/"))) {
        QStorageInfo rootStorage(QStringLiteral("/"));
        if (rootStorage.isValid()) {
            list.prepend(DriveInfo(rootStorage));
        }
    }
#endif

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
