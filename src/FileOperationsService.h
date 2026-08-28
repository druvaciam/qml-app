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
    // True while the operation is being driven by the OS shell, which shows its
    // own progress dialog with a working Cancel. Our modal must stay hidden then.
    Q_PROPERTY(bool nativeProgress READ nativeProgress NOTIFY nativeProgressChanged)

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
    bool nativeProgress() const { return m_nativeProgress; }

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
     * @brief Compare two paths for pointing at the same location.
     * Cleans both paths and, on Windows, compares case-insensitively so that
     * "C:/Docs" and "c:/docs" are recognised as the same directory.
     */
    static bool isSamePath(const QString &pathA, const QString &pathB);

    /**
     * @brief True when destinationDir is a folder inside sourcePath.
     * Copying or moving a directory into its own descendant would recurse forever.
     */
    static bool destinationInsideSource(const QString &sourcePath, const QString &destinationDir);

    /**
     * @brief True when name is a single file name, with no path separators and
     * not "." or "..". Used to keep New Folder and Rename inside the panel.
     */
    static bool isPlainFileName(const QString &name);

    /**
     * @brief Request cancellation of the currently active asynchronous operation.
     */
    Q_INVOKABLE void cancel();

signals:
    void isBusyChanged(bool busy);
    void operationTitleChanged(const QString &title);
    void progressChanged();
    void statusMessageChanged(const QString &message);
    void nativeProgressChanged(bool native);
    void operationCompleted(bool success, const QString &message);
    void operationError(const QString &error);

private:
    void setBusy(bool busy);
    void setOperationTitle(const QString &title);
    void setStatusMessage(const QString &msg);
    void setNativeProgress(bool native);
    bool runPortableCopy(const QStringList &sourcePaths, const QString &destinationDir, QString &error);
    bool checkNotIntoOwnSubfolder(const QStringList &sourcePaths, const QString &destinationDir, const QString &verb);
    void updateProgress(const QString &fileName, int processed, int total);
    void startOperation(const QString &title, const QString &status, std::function<bool()> work);

    // processed/total/error are worker-thread locals passed down the recursion.
    // They must not be members: startOperation and the queued progress update
    // both write the members from the GUI thread while the worker runs.
    bool copyRecursively(const QString &srcPath, const QString &dstPath,
                         int &processed, int total, QString &error);
    bool moveRecursively(const QString &srcPath, const QString &dstPath,
                         int &processed, int total, QString &error);
    bool deleteRecursively(const QString &path, int &processed, int total, QString &error);
    int countTotalItems(const QStringList &paths);

    bool m_isBusy = false;
    QString m_operationTitle;
    QString m_currentFileName;
    int m_processedItems = 0;
    int m_totalItems = 0;
    double m_progress = 0.0;
    QString m_statusMessage;
    QString m_lastError;
    bool m_nativeProgress = false;

    QAtomicInt m_cancelRequested{0};
    QFutureWatcher<bool> m_futureWatcher;
};
