/**
 * @file FileOperationsService.h
 * @brief Centralized backend engine for executing file system mutations (I/O).
 *
 * ARCHITECTURAL ROLE:
 * FileOperationsService is the low-level, non-UI worker service responsible for
 * executing physical file system changes on disk:
 *   - Asynchronous Copy / Move operations with progress reporting and cancellation.
 *   - Permanent deletion and Recycle Bin deletion (F8 / Shift+Delete).
 *   - Directory creation and atomic file renaming.
 *   - Native OS Shell integration (Windows IFileOperation) to seamlessly handle
 *     UAC Administrator elevation on protected paths (e.g. C:\ root) like File Explorer.
 *
 * DISTINCTION FROM PanelController:
 *   - FileOperationsService has NO concept of UI panels, active focus, tabs, current
 *     directory, navigation history, or selection models. It only accepts raw paths
 *     and performs disk I/O asynchronously off the UI thread.
 *   - PanelController, in contrast, manages the UI state of a single view panel (path,
 *     history, selection, drive info) and delegates actual disk operations to this service.
 */

#pragma once

#include <QObject>
#include <QStringList>
#include <QFutureWatcher>
#include <QAtomicInt>
#include <QtQml/qqmlregistration.h>

class FileOperationsService : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // Asynchronous Progress & State Properties
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY isBusyChanged)
    Q_PROPERTY(QString operationTitle READ operationTitle NOTIFY operationTitleChanged)
    Q_PROPERTY(QString currentFileName READ currentFileName NOTIFY progressChanged)
    Q_PROPERTY(int processedItems READ processedItems NOTIFY progressChanged)
    Q_PROPERTY(int totalItems READ totalItems NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit FileOperationsService(QObject *parent = nullptr);
    ~FileOperationsService() override;

    // State getters
    bool isBusy() const { return m_isBusy; }
    QString operationTitle() const { return m_operationTitle; }
    QString currentFileName() const { return m_currentFileName; }
    int processedItems() const { return m_processedItems; }
    int totalItems() const { return m_totalItems; }
    double progress() const { return m_progress; }
    QString statusMessage() const { return m_statusMessage; }

    /**
     * @brief Asynchronously copy multiple files/directories to destinationDir.
     * Uses Windows Shell IFileOperation on Windows for native progress & UAC elevation.
     */
    Q_INVOKABLE void copyItems(const QStringList &sourcePaths, const QString &destinationDir);

    /**
     * @brief Asynchronously move multiple files/directories to destinationDir.
     */
    Q_INVOKABLE void moveItems(const QStringList &sourcePaths, const QString &destinationDir);

    /**
     * @brief Asynchronously delete items.
     * @param permanent If true, deletes directly; if false, sends to Windows Recycle Bin.
     */
    Q_INVOKABLE void deleteItems(const QStringList &paths, bool permanent = false);

    /**
     * @brief Synchronously create a new directory inside parentPath.
     */
    Q_INVOKABLE bool createDirectory(const QString &parentPath, const QString &dirName);

    /**
     * @brief Rename an item on disk, with UAC elevation fallback on Windows.
     * Emits operationCompleted or operationError signals.
     */
    Q_INVOKABLE bool renameItem(const QString &oldPath, const QString &newName);

    /**
     * @brief Static helper to rename a file/folder with UAC elevation fallback.
     * Usable by PanelController and other components without signal side-effects.
     */
    static bool performRename(const QString &oldPath, const QString &newName, QString *errorOut = nullptr);

    /**
     * @brief Generate unique copy name when copying into same directory (e.g. "foo - copy.txt", "foo - copy (2).txt").
     */
    static QString generateCopyName(const QString &sourcePath, const QString &destinationDir);

    /**
     * @brief Request cancellation of the currently active asynchronous operation.
     */
    Q_INVOKABLE void cancel();

signals:
    void isBusyChanged(bool busy);
    void operationTitleChanged(const QString &title);
    void progressChanged();
    void statusMessageChanged(const QString &message);
    void operationCompleted(bool success, const QString &message);
    void operationError(const QString &error);

private:
    void setBusy(bool busy);
    void setOperationTitle(const QString &title);
    void setStatusMessage(const QString &msg);
    void updateProgress(const QString &fileName, int processed, int total);
    void startOperation(const QString &title, const QString &status, std::function<bool()> work);

    bool copyRecursively(const QString &srcPath, const QString &dstPath);
    bool moveRecursively(const QString &srcPath, const QString &dstPath);
    bool deleteRecursively(const QString &path);
    int countTotalItems(const QStringList &paths);

    bool m_isBusy = false;
    QString m_operationTitle;
    QString m_currentFileName;
    int m_processedItems = 0;
    int m_totalItems = 0;
    double m_progress = 0.0;
    QString m_statusMessage;
    QString m_lastError;

    QAtomicInt m_cancelRequested{0};
    QFutureWatcher<bool> m_futureWatcher;
};
