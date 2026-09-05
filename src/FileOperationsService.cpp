#include "FileOperationsService.h"
#include "Logging.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <QMetaObject>
#include <QRegularExpression>
#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>

enum class ShellOp { Copy, Move, Delete, Trash, Rename };

using ShellReporter = std::function<void(const QString &, int, int)>;
using ShellCancelCheck = std::function<bool()>;
using ShellPathNoter = std::function<void(const QString &)>;

/// The shell picks the final name itself when it resolves a collision, so the
/// only way to know what a copy actually produced is to be told. IFileOperation
/// hands the newly created item to the Post* callbacks; this turns it into a
/// path we can hand to the panels.
static QString shellItemPath(IShellItem *item)
{
    if (!item) {
        return QString();
    }
    LPWSTR raw = nullptr;
    QString path;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) {
        path = QDir::fromNativeSeparators(QString::fromWCharArray(raw));
        CoTaskMemFree(raw);
    }
    return path;
}

/**
 * Receives progress from IFileOperation so the app can draw its own dialog.
 * Without this the only option is the shell's progress window, which looks
 * nothing like the rest of the app and cannot be styled.
 *
 * Runs on the worker thread that called PerformOperations; the reporter only
 * stores into atomics, so that is safe.
 */
class ShellProgressSink final : public IFileOperationProgressSink
{
public:
    ShellProgressSink(ShellReporter reporter, ShellCancelCheck cancelled,
                      ShellPathNoter created, ShellPathNoter removed)
        : m_report(std::move(reporter)), m_cancelled(std::move(cancelled)),
          m_noteCreated(std::move(created)), m_noteRemoved(std::move(removed)) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
            *ppv = static_cast<IFileOperationProgressSink *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const LONG remaining = InterlockedDecrement(&m_ref);
        if (remaining == 0) delete this;
        return remaining;
    }

    IFACEMETHODIMP UpdateProgress(UINT workTotal, UINT workSoFar) override
    {
        m_total = static_cast<int>(workTotal);
        m_soFar = static_cast<int>(workSoFar);
        m_report(m_current, m_soFar, m_total);
        return abortIfCancelled();
    }

    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem *item, IShellItem *, LPCWSTR, HRESULT hr,
                                IShellItem *created) override
    { noteItem(item); recordCreated(hr, created); return S_OK; }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem *item, IShellItem *, LPCWSTR, HRESULT hr,
                                IShellItem *created) override
    {
        noteItem(item);
        recordCreated(hr, created);
        if (SUCCEEDED(hr) && m_noteRemoved) {
            m_noteRemoved(shellItemPath(item));
        }
        return S_OK;
    }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem *item, HRESULT hr, IShellItem *) override
    {
        noteItem(item);
        if (SUCCEEDED(hr) && m_noteRemoved) {
            m_noteRemoved(shellItemPath(item));
        }
        return S_OK;
    }

    // The rest of the interface has to exist, but there is nothing to report.
    IFACEMETHODIMP StartOperations() override { return S_OK; }
    IFACEMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem *, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem *item, LPCWSTR, HRESULT hr,
                                  IShellItem *created) override
    {
        if (SUCCEEDED(hr) && m_noteRemoved) {
            m_noteRemoved(shellItemPath(item));
        }
        recordCreated(hr, created);
        return S_OK;
    }
    // Returning a failure from a Pre* callback stops IFileOperation. This is the
    // only way to cancel it: the service's cancel flag is not something the shell
    // ever looks at, so before this the Cancel button did nothing at all.
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override { return abortIfCancelled(); }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override { return abortIfCancelled(); }
    IFACEMETHODIMP PreDeleteItem(DWORD, IShellItem *) override { return abortIfCancelled(); }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem *, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD, HRESULT hr,
                               IShellItem *created) override
    { recordCreated(hr, created); return S_OK; }
    IFACEMETHODIMP ResetTimer() override { return S_OK; }
    IFACEMETHODIMP PauseTimer() override { return S_OK; }
    IFACEMETHODIMP ResumeTimer() override { return S_OK; }

private:
    HRESULT abortIfCancelled() const
    {
        return (m_cancelled && m_cancelled()) ? E_ABORT : S_OK;
    }

    void recordCreated(HRESULT hr, IShellItem *created)
    {
        if (SUCCEEDED(hr) && created && m_noteCreated) {
            m_noteCreated(shellItemPath(created));
        }
    }

    void noteItem(IShellItem *item)
    {
        if (item) {
            LPWSTR display = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEEDITING, &display)) && display) {
                m_current = QString::fromWCharArray(display);
                CoTaskMemFree(display);
            }
        }
        m_report(m_current, m_soFar, m_total);
    }

    LONG m_ref = 1;
    ShellReporter m_report;
    ShellCancelCheck m_cancelled;
    ShellPathNoter m_noteCreated;
    ShellPathNoter m_noteRemoved;
    QString m_current;
    int m_total = 0;
    int m_soFar = 0;
};

static bool runWindowsShellOp(HWND hwndOwner, ShellOp op, const QStringList &sources, const QString &destDir,
                              QString &errorMessage, int policy = 0, ShellReporter reporter = {},
                              ShellCancelCheck isCancelled = {},
                              ShellPathNoter noteCreated = {}, ShellPathNoter noteRemoved = {})
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needUninit = SUCCEEDED(hr);

    IFileOperation *pfo = nullptr;
    hr = CoCreateInstance(CLSID_FileOperation, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pfo));
    if (!SUCCEEDED(hr)) {
        errorMessage = QStringLiteral("Failed to initialize Windows Shell FileOperation.");
        if (needUninit) CoUninitialize();
        return false;
    }

    // Conflicts have already been put to the user through our own dialog, the
    // same one Linux uses, so the shell must not ask a second question with
    // different wording and different options. Skip removed the clashing items
    // before we got here and Keep both renamed them, so by this point nothing
    // is left to confirm.
    DWORD flags = FOF_NOCONFIRMMKDIR | FOF_NOCONFIRMATION;
    if (op != ShellOp::Delete) {
        flags |= FOF_ALLOWUNDO; // Undoable copy/move or Recycle Bin
    }
    if (reporter) {
        // Suppress the shell's own progress window; we draw our own from the
        // sink below. Error and elevation prompts are unaffected.
        flags |= FOF_SILENT;
    }
    pfo->SetOperationFlags(flags);
    if (hwndOwner) {
        pfo->SetOwnerWindow(hwndOwner);
    }

    ShellProgressSink *sink = nullptr;
    DWORD sinkCookie = 0;
    if (reporter) {
        sink = new ShellProgressSink(std::move(reporter), isCancelled,
                                     std::move(noteCreated), std::move(noteRemoved));
        if (FAILED(pfo->Advise(sink, &sinkCookie))) {
            sink->Release();
            sink = nullptr;
        }
    }

    IShellItem *psiDest = nullptr;
    if (op == ShellOp::Copy || op == ShellOp::Move) {
        std::wstring dstW = QDir::toNativeSeparators(QDir::cleanPath(destDir)).toStdWString();
        hr = SHCreateItemFromParsingName(dstW.c_str(), NULL, IID_PPV_ARGS(&psiDest));
        if (!SUCCEEDED(hr)) {
            errorMessage = QStringLiteral("Invalid destination directory: %1").arg(destDir);
            pfo->Release();
            if (needUninit) CoUninitialize();
            return false;
        }
    }

    FileOperationsService::NameBatch nameBatch;
    for (const QString &src : sources) {
        if (isCancelled && isCancelled()) break;
        std::wstring srcW = QDir::toNativeSeparators(QDir::cleanPath(src)).toStdWString();
        IShellItem *psiItem = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(srcW.c_str(), NULL, IID_PPV_ARGS(&psiItem)))) {
            if (op == ShellOp::Copy) {
                // forceUnique when the user chose "Keep both"; otherwise this
                // only renames for a copy into the folder the item already
                // lives in, which is where "x - copy.txt" comes from.
                QString copyName = FileOperationsService::generateCopyName(
                    src, destDir, policy == FileOperationsService::KeepBoth, &nameBatch);
                if (copyName != QFileInfo(src).fileName()) {
                    std::wstring copyNameW = copyName.toStdWString();
                    pfo->CopyItem(psiItem, psiDest, copyNameW.c_str(), NULL);
                } else {
                    pfo->CopyItem(psiItem, psiDest, NULL, NULL);
                }
            } else if (op == ShellOp::Move) {
                if (policy == FileOperationsService::KeepBoth) {
                    QString moveName = FileOperationsService::generateCopyName(src, destDir, true, &nameBatch);
                    std::wstring moveNameW = moveName.toStdWString();
                    pfo->MoveItem(psiItem, psiDest, moveNameW.c_str(), NULL);
                } else {
                    pfo->MoveItem(psiItem, psiDest, NULL, NULL);
                }
            } else if (op == ShellOp::Rename) {
                std::wstring newNameW = destDir.toStdWString();
                pfo->RenameItem(psiItem, newNameW.c_str(), NULL);
            } else {
                pfo->DeleteItem(psiItem, NULL);
            }
            psiItem->Release();
        }
    }

    hr = pfo->PerformOperations();

    BOOL anyAborted = FALSE;
    pfo->GetAnyOperationsAborted(&anyAborted);

    if (sink) {
        pfo->Unadvise(sinkCookie);
        sink->Release();
    }

    if (psiDest) psiDest->Release();
    pfo->Release();
    if (needUninit) CoUninitialize();

    // E_ABORT from the sink, the shell's own cancel, or an aborted operation all
    // mean the same thing to the user.
    if (hr == E_ABORT || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || anyAborted
        || (isCancelled && isCancelled())) {
        errorMessage = QStringLiteral("Operation cancelled by user.");
        return false;
    }

    if (!SUCCEEDED(hr)) {
        errorMessage = QStringLiteral("Operation failed (error code 0x%1)").arg(static_cast<ulong>(hr), 8, 16, QChar('0'));
        return false;
    }

    return true;
}
#endif

FileOperationsService::FileOperationsService(QObject *parent)
    : QObject(parent)
{
    m_progressTimer.setInterval(80);
    connect(&m_progressTimer, &QTimer::timeout, this, &FileOperationsService::publishProgress);

    // Anything finishing inside this window never puts a dialog on screen.
    m_showProgressTimer.setSingleShot(true);
    m_showProgressTimer.setInterval(400);
    connect(&m_showProgressTimer, &QTimer::timeout, this, [this]() { setProgressVisible(true); });

    connect(&m_futureWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        bool success = m_futureWatcher.result();
        m_progressTimer.stop();
        m_showProgressTimer.stop();
        publishProgress();
        setProgressVisible(false);
        setBusy(false);
        setNativeProgress(false);
        // Check success first. A cancel request that arrived too late to stop the
        // work must not report a completed operation as cancelled.
        if (success) {
            emit operationCompleted(true, QStringLiteral("Operation completed successfully."));
        } else if (m_cancelRequested.loadAcquire()) {
            emit operationCompleted(false, QStringLiteral("Operation cancelled by user."));
        } else {
            QString err = m_lastError.isEmpty() ? QStringLiteral("Operation finished with errors.") : m_lastError;
            emit operationCompleted(false, err);
        }
    });
}

FileOperationsService::~FileOperationsService()
{
    cancel();
    m_futureWatcher.waitForFinished();
}

void FileOperationsService::beginDelta()
{
    QMutexLocker lock(&m_deltaMutex);
    qCDebug(lcOps) << "recording what this operation touches";
    m_lastRemovedPaths.clear();
    m_lastCreatedPaths.clear();
    m_lastChangeIsKnown = true;
}

void FileOperationsService::noteRemoved(const QString &path)
{
    QMutexLocker lock(&m_deltaMutex);
    m_lastRemovedPaths << path;
}

void FileOperationsService::noteCreated(const QString &path)
{
    QMutexLocker lock(&m_deltaMutex);
    m_lastCreatedPaths << path;
}

void FileOperationsService::abandonDelta()
{
    QMutexLocker lock(&m_deltaMutex);
    qCDebug(lcOps) << "cannot describe what changed - the panels will do a full refresh";
    m_lastChangeIsKnown = false;
}

void FileOperationsService::setBusy(bool busy)
{
    if (m_isBusy != busy) {
        m_isBusy = busy;
        emit isBusyChanged(m_isBusy);
    }
}

void FileOperationsService::setOperationTitle(const QString &title)
{
    if (m_operationTitle != title) {
        m_operationTitle = title;
        emit operationTitleChanged(m_operationTitle);
    }
}

void FileOperationsService::setNativeProgress(bool native)
{
    if (m_nativeProgress != native) {
        m_nativeProgress = native;
        emit nativeProgressChanged(m_nativeProgress);
    }
}

void FileOperationsService::setProgressVisible(bool visible)
{
    if (m_progressVisible != visible) {
        m_progressVisible = visible;
        emit progressVisibleChanged(m_progressVisible);
    }
}

void FileOperationsService::setProgressIsItemCount(bool isCount)
{
    if (m_progressIsItemCount != isCount) {
        m_progressIsItemCount = isCount;
        emit progressIsItemCountChanged(m_progressIsItemCount);
    }
}

void FileOperationsService::setStatusMessage(const QString &msg)
{
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusMessageChanged(m_statusMessage);
    }
}

void FileOperationsService::updateProgress(const QString &fileName, int processed, int total)
{
    m_workerProcessed.storeRelease(processed);
    m_workerTotal.storeRelease(total);
    QMutexLocker locker(&m_workerNameMutex);
    m_workerFileName = fileName;
}

void FileOperationsService::publishProgress()
{
    const int processed = m_workerProcessed.loadAcquire();
    const int total = m_workerTotal.loadAcquire();
    QString name;
    {
        QMutexLocker locker(&m_workerNameMutex);
        name = m_workerFileName;
    }

    if (processed == m_processedItems && total == m_totalItems && name == m_currentFileName) {
        return;
    }

    m_processedItems = processed;
    m_totalItems = total;
    m_currentFileName = name;
    m_progress = (total > 0) ? (static_cast<double>(processed) / static_cast<double>(total)) : 0.0;
    emit progressChanged();
}

void FileOperationsService::startOperation(const QString &title, const QString &status, std::function<bool()> work)
{
    m_cancelRequested.storeRelease(0);
    setBusy(true);
    setOperationTitle(title);
    setStatusMessage(status);
    // The total was NOT being cleared, so a new operation briefly showed
    // "0 of <previous total>" until the worker had finished counting.
    m_workerProcessed.storeRelease(0);
    m_workerTotal.storeRelease(0);
    {
        QMutexLocker locker(&m_workerNameMutex);
        m_workerFileName.clear();
    }
    m_processedItems = 0;
    m_totalItems = 0;
    m_currentFileName.clear();
    m_progress = 0.0;
    m_lastError.clear();
    emit progressChanged();

    m_progressTimer.start();
    m_showProgressTimer.start();
    m_futureWatcher.setFuture(QtConcurrent::run(std::move(work)));
}

void FileOperationsService::cancel()
{
    m_cancelRequested.storeRelease(1);
    setStatusMessage(QStringLiteral("Cancelling operation..."));
}

int FileOperationsService::countTotalItems(const QStringList &paths)
{
    int total = 0;
    for (const QString &path : paths) {
        QFileInfo info(path);
        if (info.isDir()) {
            total++;
            QDir dir(path);
            QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
            QStringList subPaths;
            for (const auto &entry : entries) {
                subPaths.append(entry.absoluteFilePath());
            }
            total += countTotalItems(subPaths);
        } else {
            total++;
        }
    }
    return total;
}

QList<int> FileOperationsService::countEachPath(const QStringList &paths)
{
    QList<int> counts;
    counts.reserve(paths.size());
    for (const QString &path : paths) {
        counts.append(countTotalItems({path}));
    }
    return counts;
}

/// Carry the source's modification date onto the copy.
///
/// QFile::copy brings content and permissions across but not timestamps, so
/// every copied file was stamped with the moment it was copied. Explorer and
/// Total Commander both preserve it, and Date is one of the columns this app
/// sorts by, so losing it reorders a folder that was only duplicated.
/// Copies are written beside the destination and swapped in only once they
/// succeed, so a failure never destroys the file already there. The scratch
/// file is an internal detail and should never be seen: the name is
/// dot-prefixed, which both this app and every Unix tool treat as hidden, and
/// FileListModel drops it from the listing whatever "show hidden" says.
static QString scratchSuffix()
{
    return QStringLiteral(".qmlcommander-part");
}

static QString scratchPathFor(const QString &dstPath)
{
    const QFileInfo info(dstPath);
    return QDir::cleanPath(info.absolutePath() + QLatin1Char('/')
                           + QLatin1Char('.') + info.fileName() + scratchSuffix());
}

/// Kill the app mid-copy and its scratch files stayed in the destination for
/// good - nothing ever swept them up. Clearing them when a copy into that
/// folder starts is the natural moment: it is the only time we know the folder
/// is ours to tidy, and any scratch file still there is certainly stale
/// because a copy is not already running.
static void removeStaleScratchFiles(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists()) return;

    const QStringList leftovers =
        dir.entryList(QStringList() << (QStringLiteral("*") + scratchSuffix()),
                      QDir::Files | QDir::Hidden | QDir::System);
    for (const QString &name : leftovers) {
        QFile::remove(dir.absoluteFilePath(name));
    }
}

static void preserveModifiedTime(const QString &path, const QDateTime &when)
{
    if (!when.isValid()) return;

    QFile file(path);
    if (file.open(QIODevice::ReadWrite)) {
        file.setFileTime(when, QFileDevice::FileModificationTime);
        return;
    }

    // A read-only file cannot be opened for writing, and permissions were
    // already copied from the source, so lift write access just long enough.
    const QFileDevice::Permissions perms = QFile::permissions(path);
    if (!QFile::setPermissions(path, perms | QFileDevice::WriteOwner)) {
        return;
    }
    if (file.open(QIODevice::ReadWrite)) {
        file.setFileTime(when, QFileDevice::FileModificationTime);
        file.close();
    }
    QFile::setPermissions(path, perms);
}

bool FileOperationsService::copyRecursively(const QString &srcPath, const QString &dstPath,
                                            int &processed, int total, QString &error)
{
    if (m_cancelRequested.loadAcquire()) return false;

    QFileInfo srcInfo(srcPath);
    if (!srcInfo.exists()) {
        error = QStringLiteral("Source does not exist: %1").arg(srcPath);
        return false;
    }

    if (srcInfo.isDir()) {
        // List the source BEFORE creating the destination. Duplicating a folder
        // into its own parent would otherwise find the new folder in this listing
        // and copy it into itself.
        QDir dir(srcPath);
        const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

        if (!QDir().mkpath(dstPath)) {
            error = QStringLiteral("Cannot create folder '%1' (Access denied or invalid path)").arg(dstPath);
            return false;
        }
        noteCreated(dstPath);

        for (const auto &entry : entries) {
            if (m_cancelRequested.loadAcquire()) return false;
            QString srcItem = entry.absoluteFilePath();
            QString dstItem = QDir::cleanPath(QDir(dstPath).filePath(entry.fileName()));

            processed++;
            updateProgress(entry.fileName(), processed, total);

            if (!copyRecursively(srcItem, dstItem, processed, total, error)) {
                return false;
            }
        }
        return true;
    } else {
        // Target file
        QString parentDir = QFileInfo(dstPath).absolutePath();
        if (!QDir().mkpath(parentDir)) {
            error = QStringLiteral("Cannot create destination directory '%1' (Access denied)").arg(parentDir);
            return false;
        }

        // Write beside the destination first and swap it in only once the copy
        // succeeded, so a failure never destroys the file already there.
        const QString partPath = scratchPathFor(dstPath);
        QFile::remove(partPath);

        QFile srcFile(srcPath);
        if (!srcFile.copy(partPath)) {
            error = QStringLiteral("Cannot copy '%1' to '%2': %3").arg(srcInfo.fileName(), dstPath, srcFile.errorString());
            QFile::remove(partPath);
            return false;
        }
        if (QFile::exists(dstPath) && !QFile::remove(dstPath)) {
            error = QStringLiteral("Cannot overwrite existing file '%1' (File in use or access denied)").arg(dstPath);
            QFile::remove(partPath);
            return false;
        }
        if (!QFile::rename(partPath, dstPath)) {
            error = QStringLiteral("Cannot finish writing '%1' (Access denied)").arg(dstPath);
            QFile::remove(partPath);
            return false;
        }

        // QFile::copy already carries the permissions across; only the
        // modification date needs restoring.
        preserveModifiedTime(dstPath, srcInfo.lastModified());
        noteCreated(dstPath);
        return true;
    }
}

bool FileOperationsService::moveRecursively(const QString &srcPath, const QString &dstPath,
                                            int &processed, int total, QString &error)
{
    if (m_cancelRequested.loadAcquire()) return false;

    QFileInfo srcInfo(srcPath);
    if (!srcInfo.exists()) {
        error = QStringLiteral("Source does not exist: %1").arg(srcPath);
        return false;
    }

    QDir().mkpath(QFileInfo(dstPath).absolutePath());

    // Plain rename only when nothing is in the way. The destination is never
    // removed up front: a later failure would leave it destroyed for nothing.
    if (!QFile::exists(dstPath) && QFile::rename(srcPath, dstPath)) {
        noteRemoved(srcPath);
        noteCreated(dstPath);
        return true;
    }

    // Fallback: copy (which swaps the destination in safely) and then delete.
    // The delete uses a scratch counter: those items were already counted by the
    // copy, and counting them twice would run the bar past 100%.
    if (copyRecursively(srcPath, dstPath, processed, total, error)) {
        int scratch = 0;
        return deleteRecursively(srcPath, scratch, total, error);
    }
    return false;
}

bool FileOperationsService::deleteRecursively(const QString &path,
                                              int &processed, int total, QString &error)
{
    if (m_cancelRequested.loadAcquire()) return false;

    QFileInfo info(path);
    if (!info.exists()) return true;

    if (info.isDir()) {
        QDir dir(path);
        const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto &entry : entries) {
            if (m_cancelRequested.loadAcquire()) return false;
            processed++;
            updateProgress(entry.fileName(), processed, total);
            if (!deleteRecursively(entry.absoluteFilePath(), processed, total, error)) {
                return false;
            }
        }
        if (!dir.rmdir(path)) {
            error = QStringLiteral("Cannot remove folder '%1' (Not empty or access denied)").arg(path);
            return false;
        }
        return true;
    }

    if (!QFile::remove(path)) {
        error = QStringLiteral("Cannot delete '%1' (File in use or access denied)").arg(path);
        return false;
    }
    return true;
}

bool FileOperationsService::isSamePath(const QString &pathA, const QString &pathB)
{
    const QString cleanA = QDir::cleanPath(pathA);
    const QString cleanB = QDir::cleanPath(pathB);
#ifdef Q_OS_WIN
    return cleanA.compare(cleanB, Qt::CaseInsensitive) == 0;
#else
    return cleanA == cleanB;
#endif
}

QStringList FileOperationsService::findConflicts(const QStringList &sourcePaths,
                                                 const QString &destinationDir, bool isMove)
{
    QStringList names;
    for (const QString &src : sourcePaths) {
        const QFileInfo srcInfo(src);
        // A copy into the folder the item already lives in becomes "x - copy",
        // so it never clashes. A move to the same folder is a no-op.
        if (isSamePath(srcInfo.absolutePath(), destinationDir)) {
            continue;
        }
        const QString target = QDir::cleanPath(QDir(destinationDir).filePath(srcInfo.fileName()));
        if (QFileInfo::exists(target)) {
            names.append(srcInfo.fileName());
        }
    }
    Q_UNUSED(isMove)
    return names;
}

bool FileOperationsService::resolveConflicts(QStringList &sourcePaths, const QString &destinationDir,
                                             bool isMove, int &policy)
{
    if (policy != Ask) {
        // Already answered. Skip drops the clashing items before we start.
        if (policy == Skip) {
            const QStringList clashing = findConflicts(sourcePaths, destinationDir, isMove);
            QStringList kept;
            for (const QString &src : sourcePaths) {
                if (!clashing.contains(QFileInfo(src).fileName())) {
                    kept.append(src);
                }
            }
            sourcePaths = kept;
            if (sourcePaths.isEmpty()) {
                emit operationCompleted(true, QStringLiteral("Nothing to do: every item was skipped."));
                return false;
            }
        }
        return true;
    }

    const QStringList clashing = findConflicts(sourcePaths, destinationDir, isMove);
    if (clashing.isEmpty()) {
        policy = Overwrite; // nothing in the way, the value no longer matters
        return true;
    }

    // Hand the decision to the UI and start nothing.
    emit conflictsFound(clashing, sourcePaths, destinationDir, isMove);
    return false;
}

bool FileOperationsService::destinationInsideSource(const QString &sourcePath, const QString &destinationDir)
{
    QFileInfo si(sourcePath);
    if (!si.isDir()) return false;
    const QString s = QDir::cleanPath(si.absoluteFilePath());
    const QString d = QDir::cleanPath(destinationDir);
#ifdef Q_OS_WIN
    return d.startsWith(s + QLatin1Char('/'), Qt::CaseInsensitive);
#else
    return d.startsWith(s + QLatin1Char('/'), Qt::CaseSensitive);
#endif
}

bool FileOperationsService::isPlainFileName(const QString &name)
{
    const QString t = name.trimmed();
    return !t.isEmpty()
        && !t.contains(QLatin1Char('/'))
        && !t.contains(QLatin1Char('\\'))
        && t != QStringLiteral(".")
        && t != QStringLiteral("..");
}

QString FileOperationsService::generateCopyName(const QString &sourcePath, const QString &destinationDir,
                                                bool forceUnique, NameBatch *batch)
{
    QFileInfo srcInfo(sourcePath);

    // Names for a whole batch are worked out before a single file is written, so
    // checking the disk alone is not enough: without `reserved`, 140 files that
    // share a base all pick the same free name and overwrite one another.
    auto key = [](const QString &name) {
#ifdef Q_OS_WIN
        return name.toLower();
#else
        return name;
#endif
    };
    auto claim = [&](const QString &name) {
        if (batch) batch->claimed.insert(key(name));
        return name;
    };
    auto isTaken = [&](const QString &name) {
        const QString k = key(name);
        if (batch && batch->claimed.contains(k)) return true;
        // Remember what the disk said. The next file with the same base then
        // skips these by lookup instead of stat'ing them all over again.
        if (batch && batch->onDisk.contains(k)) return true;
        if (QFileInfo::exists(QDir::cleanPath(QDir(destinationDir).filePath(name)))) {
            if (batch) batch->onDisk.insert(k);
            return true;
        }
        return false;
    };

    // Copying into a different folder keeps the original name, unless the caller
    // asked for a name that cannot clash (the "Keep both" answer).
    if (!isSamePath(srcInfo.absolutePath(), destinationDir)) {
        const QString plain = srcInfo.fileName();
        if (!forceUnique) {
            // An existing file here is deliberate - Replace was chosen. Only a
            // name already claimed by this same batch forces a new one, which is
            // why `claimed` and `onDisk` have to stay separate sets.
            if (!(batch && batch->claimed.contains(key(plain)))) {
                return claim(plain);
            }
        } else if (!isTaken(plain)) {
            return claim(plain);
        }
    }

    // Determine base name and extension
    QString base;
    QString ext;
    if (srcInfo.isDir()) {
        base = srcInfo.fileName();
        ext = QString();
    } else {
        base = srcInfo.completeBaseName();
        ext = srcInfo.suffix();
        if (base.isEmpty()) {
            base = srcInfo.fileName();
            ext = QString();
        }
    }

    // Check if base already ends with " - copy" or " - copy (N)"
    static const QRegularExpression rxCopy(QStringLiteral(R"(^(.*) - copy(?:\s*\((\d+)\))?$)"), QRegularExpression::CaseInsensitiveOption);
    auto match = rxCopy.match(base);
    int startIndex = 1;
    if (match.hasMatch()) {
        base = match.captured(1);
        if (match.captured(2).isEmpty()) {
            startIndex = 2;
        } else {
            startIndex = match.captured(2).toInt() + 1;
        }
    }

    auto makeCandidate = [&base, &ext](int index) -> QString {
        QString name;
        if (index == 1) {
            name = QStringLiteral("%1 - copy").arg(base);
        } else {
            name = QStringLiteral("%1 - copy (%2)").arg(base).arg(index);
        }
        if (!ext.isEmpty()) {
            name += QStringLiteral(".") + ext;
        }
        return name;
    };

    // Resume from where the previous file with this base name finished.
    const QString baseKey = key(base + QLatin1Char('|') + ext);
    int idx = startIndex;
    if (batch) {
        idx = qMax(idx, batch->nextIndex.value(baseKey, startIndex));
    }

    QString candidateName = makeCandidate(idx);
    while (isTaken(candidateName)) {
        idx = (idx == 1) ? 2 : idx + 1;
        candidateName = makeCandidate(idx);
    }

    if (batch) {
        batch->nextIndex.insert(baseKey, (idx == 1) ? 2 : idx + 1);
    }
    return claim(candidateName);
}

void FileOperationsService::copyItems(const QStringList &sourcePaths, const QString &destinationDir,
                                      int policy)
{
    if (m_isBusy || sourcePaths.isEmpty() || destinationDir.isEmpty()) return;
    if (!checkNotIntoOwnSubfolder(sourcePaths, destinationDir, QStringLiteral("copy"))) return;

    QStringList sources = sourcePaths;
    // Every platform asks the same question, with the same wording and the same
    // options. Windows used to fall through to the shell's own Replace-or-Skip
    // dialog, which behaves differently and offers different choices.
    if (!resolveConflicts(sources, destinationDir, false, policy)) return;

    m_lastSourcePaths = sources;

#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    setProgressIsItemCount(false);
    startOperation(QStringLiteral("Copying files..."), QStringLiteral("Preparing copy..."), [this, hwnd, sources, destinationDir, policy]() {
        QString err;
        beginDelta();
        auto reporter = [this](const QString &name, int done, int total) { updateProgress(name, done, total); };
        auto cancelled = [this]() { return m_cancelRequested.loadAcquire() != 0; };
        auto created = [this](const QString &p) { noteCreated(p); };
        auto removed = [this](const QString &p) { noteRemoved(p); };
        bool ok = runWindowsShellOp(hwnd, ShellOp::Copy, sources, destinationDir, err, policy, reporter,
                                    cancelled, created, removed);
        if (!ok && err != QStringLiteral("Operation cancelled by user.")) {
            // Fallback manual copy if shell operation failed
            QMetaObject::invokeMethod(this, [this]() { setProgressIsItemCount(true); }, Qt::QueuedConnection);
            QString fallbackErr;
            ok = runPortableCopy(sources, destinationDir, policy, fallbackErr);
            if (!ok) err = fallbackErr.isEmpty() ? err : fallbackErr;
        }
        if (!ok) m_lastError = err;
        return ok;
    });
#else
    setProgressIsItemCount(true);
    startOperation(QStringLiteral("Copying files..."), QStringLiteral("Preparing copy..."), [this, sources, destinationDir, policy]() {
        QString err;
        beginDelta();
        bool ok = runPortableCopy(sources, destinationDir, policy, err);
        if (!ok) {
            m_lastError = err;
            // A half-finished copy is not something we can describe exactly.
            abandonDelta();
        }
        return ok;
    });
#endif
}

bool FileOperationsService::runPortableCopy(const QStringList &sourcePaths, const QString &destinationDir,
                                            int policy, QString &error)
{
    const int total = countTotalItems(sourcePaths);
    m_workerTotal.storeRelease(total);

    removeStaleScratchFiles(destinationDir);

    bool allOk = true;
    int processed = 0;
    NameBatch nameBatch;
    for (const QString &src : sourcePaths) {
        if (m_cancelRequested.loadAcquire()) return false;
        QFileInfo srcInfo(src);
        QString copyName = generateCopyName(src, destinationDir, policy == KeepBoth, &nameBatch);
        QString dst = QDir::cleanPath(QDir(destinationDir).filePath(copyName));
        processed++;
        updateProgress(srcInfo.fileName(), processed, total);
        if (!copyRecursively(src, dst, processed, total, error)) allOk = false;
    }
    return allOk;
}

void FileOperationsService::moveItems(const QStringList &sourcePaths, const QString &destinationDir,
                                      int policy)
{
    if (m_isBusy || sourcePaths.isEmpty() || destinationDir.isEmpty()) return;
    if (!checkNotIntoOwnSubfolder(sourcePaths, destinationDir, QStringLiteral("move"))) return;

    QStringList sources = sourcePaths;
    if (!resolveConflicts(sources, destinationDir, true, policy)) return;

    m_lastSourcePaths = sources;

#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    setProgressIsItemCount(false);
    startOperation(QStringLiteral("Moving files..."), QStringLiteral("Preparing move..."), [this, hwnd, sources, destinationDir, policy]() {
        QString err;
        beginDelta();
        auto reporter = [this](const QString &name, int done, int total) { updateProgress(name, done, total); };
        auto cancelled = [this]() { return m_cancelRequested.loadAcquire() != 0; };
        auto created = [this](const QString &p) { noteCreated(p); };
        auto removed = [this](const QString &p) { noteRemoved(p); };
        bool ok = runWindowsShellOp(hwnd, ShellOp::Move, sources, destinationDir, err, policy, reporter,
                                    cancelled, created, removed);
        if (!ok) m_lastError = err;
        return ok;
    });
#else
    setProgressIsItemCount(true);
    startOperation(QStringLiteral("Moving files..."), QStringLiteral("Preparing move..."), [this, sources, destinationDir, policy]() {
        const QList<int> counts = countEachPath(sources);
        int total = 0;
        for (int c : counts) total += c;
        m_workerTotal.storeRelease(total);

        bool allOk = true;
        int processed = 0;
        int credited = 0;   // items fully accounted for by finished sources
        QString err;
        NameBatch nameBatch;
        for (int i = 0; i < sources.size(); ++i) {
            if (m_cancelRequested.loadAcquire()) return false;
            const QString &src = sources.at(i);
            QFileInfo srcInfo(src);
            const QString name = generateCopyName(src, destinationDir, policy == KeepBoth, &nameBatch);
            QString dst = QDir::cleanPath(QDir(destinationDir).filePath(name));
            if (isSamePath(src, dst)) {
                credited += counts.at(i);
                processed = credited;
                continue;
            }
            updateProgress(srcInfo.fileName(), processed, total);
            if (!moveRecursively(src, dst, processed, total, err)) allOk = false;

            // A rename moves a whole subtree in one call and advances nothing,
            // so bring the counter up to where this source ends either way.
            credited += counts.at(i);
            if (processed < credited) processed = credited;
            updateProgress(srcInfo.fileName(), processed, total);
        }
        if (!allOk) m_lastError = err;
        return allOk;
    });
#endif
}

bool FileOperationsService::checkNotIntoOwnSubfolder(const QStringList &sourcePaths,
                                                     const QString &destinationDir,
                                                     const QString &verb)
{
    for (const QString &src : sourcePaths) {
        if (destinationInsideSource(src, destinationDir)) {
            emit operationCompleted(false,
                QStringLiteral("Cannot %1 '%2' into a folder inside itself.")
                    .arg(verb, QFileInfo(src).fileName()));
            return false;
        }
    }
    return true;
}

void FileOperationsService::deleteItems(const QStringList &paths, bool permanent)
{
    if (m_isBusy || paths.isEmpty()) return;

    m_lastSourcePaths = paths;

    QString title = permanent ? QStringLiteral("Permanently deleting...") : QStringLiteral("Moving to Recycle Bin...");
    QString status = permanent ? QStringLiteral("Deleting permanently...") : QStringLiteral("Moving to Recycle Bin...");

    setProgressIsItemCount(true);
    startOperation(title, status, [this, paths, permanent]() {
        const QList<int> counts = countEachPath(paths);
        int total = 0;
        for (int c : counts) total += c;
        m_workerTotal.storeRelease(total);

        bool allOk = true;
        int processed = 0;
        int credited = 0;
        QString err;
        QStringList fallbackPaths;
        beginDelta();

        for (int i = 0; i < paths.size(); ++i) {
            if (m_cancelRequested.loadAcquire()) return false;
            const QString &path = paths.at(i);
            QFileInfo info(path);
            updateProgress(info.fileName(), processed, total);

            // Every failure records why, and the first one wins - it is the one
            // the user can act on. Previously only deleteRecursively wrote here,
            // so a plain file that would not go left no reason at all and the
            // report fell back to a generic sentence naming nothing.
            auto noteFailure = [&err](const QString &reason) {
                if (err.isEmpty()) err = reason;
            };

            if (permanent) {
                if (info.isDir()) {
                    QString why;
                    if (!deleteRecursively(path, processed, total, why)) {
                        noteFailure(why);
                        fallbackPaths.append(path);
                    }
                } else {
                    // Try direct file removal
                    if (!QFile::remove(path)) {
                        // Clear read-only attribute if set and retry
                        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
                        if (!QFile::remove(path)) {
                            noteFailure(QStringLiteral("Cannot delete '%1' (File in use or access denied)").arg(path));
                            fallbackPaths.append(path);
                        }
                    }
                }
            } else {
                // moveToTrash takes the whole subtree in one call.
                if (!QFile::moveToTrash(path)) {
                    noteFailure(QStringLiteral("Cannot move '%1' to the Recycle Bin (in use or access denied)").arg(path));
                    fallbackPaths.append(path);
                }
            }

            // Anything not queued for the shell fallback is gone, and the panel
            // can drop its row at once instead of waiting for a rescan.
            if (fallbackPaths.isEmpty() || fallbackPaths.constLast() != path) {
                noteRemoved(path);
            }

            credited += counts.at(i);
            if (processed < credited) processed = credited;
            updateProgress(info.fileName(), processed, total);
        }

#ifdef Q_OS_WIN
        // If direct deletion failed (e.g. system folder requiring UAC elevation), fallback to Windows Shell IFileOperation
        if (!fallbackPaths.isEmpty() && !m_cancelRequested.loadAcquire()) {
            // The shell fallback runs without a progress sink, so what it
            // manages to remove is not reported back. Fall back to a refresh.
            abandonDelta();
            HWND hwnd = GetForegroundWindow();
            // Named apart from the outer `err` on purpose. This is the shell's
            // account of its own retry; the outer one records why the direct
            // attempt failed, which is not worth reporting here because the
            // shell often goes on to succeed with elevation.
            QString shellErr;
            bool shellOk = runWindowsShellOp(hwnd, permanent ? ShellOp::Delete : ShellOp::Trash, fallbackPaths, QString(), shellErr);
            if (!shellOk) {
                m_lastError = shellErr.isEmpty()
                    ? QStringLiteral("Cannot delete selected items (Permission denied or in use).")
                    : shellErr;
                allOk = false;
            }
        } else if (!fallbackPaths.isEmpty()) {
            allOk = false;
            abandonDelta();
        }
#else
        if (!fallbackPaths.isEmpty()) {
            allOk = false;
            abandonDelta();
            // There is no shell retry on this platform, so the reason recorded
            // above is the only accurate account of what went wrong - and it
            // names the offending item, which "selected items" cannot.
            if (err.isEmpty()) {
                err = QStringLiteral("Cannot delete '%1' (Permission denied or in use)")
                          .arg(fallbackPaths.first());
            }
            m_lastError = fallbackPaths.size() > 1
                ? QStringLiteral("%1 - and %2 more item(s) could not be deleted.")
                      .arg(err).arg(fallbackPaths.size() - 1)
                : err;
        }
#endif

        return allOk;
    });
}

bool FileOperationsService::createDirectory(const QString &parentPath, const QString &dirName)
{
    if (parentPath.isEmpty() || !isPlainFileName(dirName)) {
        emit operationError(QStringLiteral("Invalid folder name. It cannot be empty or contain \\ or /."));
        return false;
    }

    m_lastSourcePaths.clear();
    beginDelta();

    QDir parentDir(parentPath);
    if (!parentDir.exists()) {
        emit operationError(QStringLiteral("Parent directory does not exist."));
        return false;
    }

    const QString name = dirName.trimmed();

    // mkdir just returns false when something is already there, which produced a
    // "Failed to create folder" that did not say why.
    if (parentDir.exists(name)) {
        emit operationError(QStringLiteral("\"%1\" already exists in this folder.").arg(name));
        return false;
    }

    bool ok = parentDir.mkdir(name);
    if (ok) {
        noteCreated(parentDir.filePath(name));
        emit operationCompleted(true, QStringLiteral("Created folder: %1").arg(name));
    } else {
        emit operationError(QStringLiteral("Could not create \"%1\" here (permission denied or the name is not allowed).").arg(name));
    }
    return ok;
}

bool FileOperationsService::performRename(const QString &oldPath, const QString &newName, QString *errorOut)
{
    QString trimmed = newName.trimmed();
    if (oldPath.isEmpty() || !isPlainFileName(trimmed)) {
        if (errorOut) *errorOut = QStringLiteral("Invalid name. It cannot be empty or contain \\ or /.");
        return false;
    }

    QFileInfo info(oldPath);
    if (!info.exists()) {
        if (errorOut) *errorOut = QStringLiteral("Target item does not exist.");
        return false;
    }
    if (info.fileName() == trimmed) return true;

    QString newPath = info.dir().filePath(trimmed);

    // QFile::rename just returns false when something is already there, which
    // produced a "Failed to rename" that did not say why.
    if (QFileInfo::exists(newPath)) {
        if (errorOut) {
            *errorOut = QStringLiteral("\"%1\" already exists in this folder.").arg(trimmed);
        }
        return false;
    }

    bool ok = QFile::rename(oldPath, newPath);
#ifdef Q_OS_WIN
    if (!ok) {
        QString err;
        HWND hwnd = GetForegroundWindow();
        ok = runWindowsShellOp(hwnd, ShellOp::Rename, {oldPath}, trimmed, err);
        if (!ok && errorOut) *errorOut = err;
    }
#endif
    return ok;
}

bool FileOperationsService::renameItem(const QString &oldPath, const QString &newName)
{
    m_lastSourcePaths.clear();
    QString err;
    beginDelta();
    bool ok = performRename(oldPath, newName, &err);
    if (ok) {
        // A rename is one row leaving and one arriving, which the panel can do
        // without re-reading the folder around it. performRename is static, so
        // the delta is recorded here where there is an object to record on.
        noteRemoved(oldPath);
        noteCreated(QFileInfo(oldPath).dir().filePath(newName.trimmed()));
        emit operationCompleted(true, QString("Renamed to: %1").arg(newName.trimmed()));
    } else {
        emit operationError(err.isEmpty() ? QString("Failed to rename item to: %1").arg(newName) : err);
    }
    return ok;
}
