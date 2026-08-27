#pragma once

#include <QString>
#include <QtQml/qqmlregistration.h>
#include <QStorageInfo>

class DriveInfo
{
    Q_GADGET
    QML_VALUE_TYPE(driveInfo)

    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath)
    Q_PROPERTY(QString name READ name WRITE setName)
    Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName)
    Q_PROPERTY(QString fileSystemType READ fileSystemType WRITE setFileSystemType)
    Q_PROPERTY(qint64 bytesTotal READ bytesTotal WRITE setBytesTotal)
    Q_PROPERTY(qint64 bytesFree READ bytesFree WRITE setBytesFree)
    Q_PROPERTY(qint64 bytesAvailable READ bytesAvailable WRITE setBytesAvailable)
    Q_PROPERTY(QString formattedTotal READ formattedTotal WRITE setFormattedTotal)
    Q_PROPERTY(QString formattedFree READ formattedFree WRITE setFormattedFree)
    Q_PROPERTY(double percentUsed READ percentUsed WRITE setPercentUsed)
    Q_PROPERTY(bool isReady READ isReady WRITE setIsReady)

public:
    DriveInfo() = default;
    explicit DriveInfo(const QStorageInfo &storage);

    QString rootPath() const { return m_rootPath; }
    void setRootPath(const QString &path) { m_rootPath = path; }

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    QString displayName() const { return m_displayName; }
    void setDisplayName(const QString &name) { m_displayName = name; }

    QString fileSystemType() const { return m_fileSystemType; }
    void setFileSystemType(const QString &fs) { m_fileSystemType = fs; }

    qint64 bytesTotal() const { return m_bytesTotal; }
    void setBytesTotal(qint64 total) { m_bytesTotal = total; }

    qint64 bytesFree() const { return m_bytesFree; }
    void setBytesFree(qint64 free) { m_bytesFree = free; }

    qint64 bytesAvailable() const { return m_bytesAvailable; }
    void setBytesAvailable(qint64 avail) { m_bytesAvailable = avail; }

    QString formattedTotal() const { return m_formattedTotal; }
    void setFormattedTotal(const QString &fmt) { m_formattedTotal = fmt; }

    QString formattedFree() const { return m_formattedFree; }
    void setFormattedFree(const QString &fmt) { m_formattedFree = fmt; }

    double percentUsed() const { return m_percentUsed; }
    void setPercentUsed(double pct) { m_percentUsed = pct; }

    bool isReady() const { return m_isReady; }
    void setIsReady(bool ready) { m_isReady = ready; }

    static QString formatBytes(qint64 bytes);
    static QList<DriveInfo> getMountedDrives();

private:
    QString m_rootPath;
    QString m_name;
    QString m_displayName;
    QString m_fileSystemType;
    qint64 m_bytesTotal = 0;
    qint64 m_bytesFree = 0;
    qint64 m_bytesAvailable = 0;
    QString m_formattedTotal;
    QString m_formattedFree;
    double m_percentUsed = 0.0;
    bool m_isReady = false;
};
