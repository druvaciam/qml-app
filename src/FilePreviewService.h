#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class FilePreviewService : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit FilePreviewService(QObject *parent = nullptr);
    ~FilePreviewService() override = default;

    Q_INVOKABLE QVariantMap loadPreview(const QString &filePath, int maxBytes = 256 * 1024);
    Q_INVOKABLE bool saveTextFile(const QString &filePath, const QString &content);
    Q_INVOKABLE bool openInDefaultApp(const QString &filePath);
    Q_INVOKABLE bool openInTerminal(const QString &dirPath);

signals:
    /**
     * @brief A file was written to disk. Emitted where the save actually
     * happens, so anything that needs to react does so no matter which caller
     * triggered it - rather than relying on one dialog to pass the news on.
     */
    void fileSaved(const QString &filePath);
};
