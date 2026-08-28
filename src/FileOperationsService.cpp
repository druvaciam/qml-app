#include "FileOperationsService.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <QMetaObject>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>

enum class ShellOp { Copy, Move, Delete, Trash, Rename };

static bool runWindowsShellOp(HWND hwndOwner, ShellOp op, const QStringList &sources, const QString &destDir, QString &errorMessage)
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

    // FOF_NOCONFIRMATION is deliberately NOT set: it answers "Yes to All" to the
    // Replace-or-Skip prompt, so same-named files at the destination would be
    // overwritten silently. Let the shell ask, exactly as Explorer does.
    DWORD flags = FOF_NOCONFIRMMKDIR;
    if (op != ShellOp::Delete) {
        flags |= FOF_ALLOWUNDO; // Undoable copy/move or Recycle Bin
    }
    pfo->SetOperationFlags(flags);
    if (hwndOwner) {
        pfo->SetOwnerWindow(hwndOwner);
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

    for (const QString &src : sources) {
        std::wstring srcW = QDir::toNativeSeparators(QDir::cleanPath(src)).toStdWString();
        IShellItem *psiItem = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(srcW.c_str(), NULL, IID_PPV_ARGS(&psiItem)))) {
            if (op == ShellOp::Copy) {
                QString copyName = FileOperationsService::generateCopyName(src, destDir);
                if (copyName != QFileInfo(src).fileName()) {
                    std::wstring copyNameW = copyName.toStdWString();
                    pfo->CopyItem(psiItem, psiDest, copyNameW.c_str(), NULL);
                } else {
                    pfo->CopyItem(psiItem, psiDest, NULL, NULL);
                }
            } else if (op == ShellOp::Move) {
                pfo->MoveItem(psiItem, psiDest, NULL, NULL);
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

    if (psiDest) psiDest->Release();
    pfo->Release();
    if (needUninit) CoUninitialize();

    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || (anyAborted && !SUCCEEDED(hr))) {
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
    connect(&m_futureWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        bool success = m_futureWatcher.result();
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

void FileOperationsService::setStatusMessage(const QString &msg)
{
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusMessageChanged(m_statusMessage);
    }
}

void FileOperationsService::updateProgress(const QString &fileName, int processed, int total)
{
    QMetaObject::invokeMethod(this, [this, fileName, processed, total]() {
        m_currentFileName = fileName;
        m_processedItems = processed;
        m_totalItems = total;
        m_progress = (total > 0) ? (static_cast<double>(processed) / static_cast<double>(total)) : 0.0;
        emit progressChanged();
    }, Qt::QueuedConnection);
}

void FileOperationsService::startOperation(const QString &title, const QString &status, std::function<bool()> work)
{
    m_cancelRequested.storeRelease(0);
    setBusy(true);
    setOperationTitle(title);
    setStatusMessage(status);
    m_processedItems = 0;
    m_progress = 0.0;
    m_lastError.clear();
    emit progressChanged();

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
        const QString partPath = dstPath + QStringLiteral(".qmlcommander-part");
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
        return true;
    }

    // Fallback: copy (which swaps the destination in safely) and then delete
    if (copyRecursively(srcPath, dstPath, processed, total, error)) {
        return deleteRecursively(srcPath, processed, total, error);
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

QString FileOperationsService::generateCopyName(const QString &sourcePath, const QString &destinationDir)
{
    QFileInfo srcInfo(sourcePath);

    // If source and destination directories are different, keep original filename
    if (!isSamePath(srcInfo.absolutePath(), destinationDir)) {
        return srcInfo.fileName();
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

    QString candidateName = makeCandidate(startIndex);
    QString candidatePath = QDir::cleanPath(QDir(destinationDir).filePath(candidateName));

    int copyIndex = (startIndex == 1) ? 2 : (startIndex + 1);
    while (QFileInfo::exists(candidatePath)) {
        candidateName = makeCandidate(copyIndex);
        candidatePath = QDir::cleanPath(QDir(destinationDir).filePath(candidateName));
        copyIndex++;
    }

    return candidateName;
}

void FileOperationsService::copyItems(const QStringList &sourcePaths, const QString &destinationDir)
{
    if (m_isBusy || sourcePaths.isEmpty() || destinationDir.isEmpty()) return;
    if (!checkNotIntoOwnSubfolder(sourcePaths, destinationDir, QStringLiteral("copy"))) return;

#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    setNativeProgress(true);
    startOperation(QStringLiteral("Copying files..."), QStringLiteral("Preparing copy..."), [this, hwnd, sourcePaths, destinationDir]() {
        QString err;
        bool ok = runWindowsShellOp(hwnd, ShellOp::Copy, sourcePaths, destinationDir, err);
        if (!ok && err != QStringLiteral("Operation cancelled by user.")) {
            // Fallback manual copy if shell operation failed
            QMetaObject::invokeMethod(this, [this]() { setNativeProgress(false); }, Qt::QueuedConnection);
            QString fallbackErr;
            ok = runPortableCopy(sourcePaths, destinationDir, fallbackErr);
            if (!ok) err = fallbackErr.isEmpty() ? err : fallbackErr;
        }
        if (!ok) m_lastError = err;
        return ok;
    });
#else
    startOperation(QStringLiteral("Copying files..."), QStringLiteral("Preparing copy..."), [this, sourcePaths, destinationDir]() {
        QString err;
        bool ok = runPortableCopy(sourcePaths, destinationDir, err);
        if (!ok) m_lastError = err;
        return ok;
    });
#endif
}

bool FileOperationsService::runPortableCopy(const QStringList &sourcePaths, const QString &destinationDir, QString &error)
{
    const int total = countTotalItems(sourcePaths);
    QMetaObject::invokeMethod(this, [this, total]() { m_totalItems = total; emit progressChanged(); }, Qt::QueuedConnection);

    bool allOk = true;
    int processed = 0;
    for (const QString &src : sourcePaths) {
        if (m_cancelRequested.loadAcquire()) return false;
        QFileInfo srcInfo(src);
        QString copyName = generateCopyName(src, destinationDir);
        QString dst = QDir::cleanPath(QDir(destinationDir).filePath(copyName));
        processed++;
        updateProgress(srcInfo.fileName(), processed, total);
        if (!copyRecursively(src, dst, processed, total, error)) allOk = false;
    }
    return allOk;
}

void FileOperationsService::moveItems(const QStringList &sourcePaths, const QString &destinationDir)
{
    if (m_isBusy || sourcePaths.isEmpty() || destinationDir.isEmpty()) return;
    if (!checkNotIntoOwnSubfolder(sourcePaths, destinationDir, QStringLiteral("move"))) return;

#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    setNativeProgress(true);
    startOperation(QStringLiteral("Moving files..."), QStringLiteral("Preparing move..."), [this, hwnd, sourcePaths, destinationDir]() {
        QString err;
        bool ok = runWindowsShellOp(hwnd, ShellOp::Move, sourcePaths, destinationDir, err);
        if (!ok) m_lastError = err;
        return ok;
    });
#else
    startOperation(QStringLiteral("Moving files..."), QStringLiteral("Preparing move..."), [this, sourcePaths, destinationDir]() {
        const int total = countTotalItems(sourcePaths);
        QMetaObject::invokeMethod(this, [this, total]() { m_totalItems = total; emit progressChanged(); }, Qt::QueuedConnection);

        bool allOk = true;
        int processed = 0;
        QString err;
        for (const QString &src : sourcePaths) {
            if (m_cancelRequested.loadAcquire()) return false;
            QFileInfo srcInfo(src);
            QString dst = QDir::cleanPath(QDir(destinationDir).filePath(srcInfo.fileName()));
            if (isSamePath(src, dst)) continue;
            processed++;
            updateProgress(srcInfo.fileName(), processed, total);
            if (!moveRecursively(src, dst, processed, total, err)) allOk = false;
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

    QString title = permanent ? QStringLiteral("Permanently deleting...") : QStringLiteral("Moving to Recycle Bin...");
    QString status = permanent ? QStringLiteral("Deleting permanently...") : QStringLiteral("Moving to Recycle Bin...");

    startOperation(title, status, [this, paths, permanent]() {
        const int total = countTotalItems(paths);
        QMetaObject::invokeMethod(this, [this, total]() { m_totalItems = total; emit progressChanged(); }, Qt::QueuedConnection);

        bool allOk = true;
        int processed = 0;
        QString err;
        QStringList fallbackPaths;

        for (const QString &path : paths) {
            if (m_cancelRequested.loadAcquire()) return false;
            QFileInfo info(path);
            processed++;
            updateProgress(info.fileName(), processed, total);

            if (permanent) {
                if (info.isDir()) {
                    if (!deleteRecursively(path, processed, total, err)) {
                        fallbackPaths.append(path);
                    }
                } else {
                    // Try direct file removal
                    if (!QFile::remove(path)) {
                        // Clear read-only attribute if set and retry
                        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
                        if (!QFile::remove(path)) {
                            fallbackPaths.append(path);
                        }
                    }
                }
            } else {
                if (!QFile::moveToTrash(path)) {
                    fallbackPaths.append(path);
                }
            }
        }

#ifdef Q_OS_WIN
        // If direct deletion failed (e.g. system folder requiring UAC elevation), fallback to Windows Shell IFileOperation
        if (!fallbackPaths.isEmpty() && !m_cancelRequested.loadAcquire()) {
            HWND hwnd = GetForegroundWindow();
            QString err;
            bool shellOk = runWindowsShellOp(hwnd, permanent ? ShellOp::Delete : ShellOp::Trash, fallbackPaths, QString(), err);
            if (!shellOk) {
                m_lastError = err.isEmpty() ? QStringLiteral("Cannot delete selected items (Permission denied or in use).") : err;
                allOk = false;
            }
        } else if (!fallbackPaths.isEmpty()) {
            allOk = false;
        }
#else
        if (!fallbackPaths.isEmpty()) {
            allOk = false;
            m_lastError = QStringLiteral("Cannot delete selected items (Permission denied or in use).");
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

    QDir parentDir(parentPath);
    if (!parentDir.exists()) {
        emit operationError(QStringLiteral("Parent directory does not exist."));
        return false;
    }

    bool ok = parentDir.mkdir(dirName.trimmed());
    if (ok) {
        emit operationCompleted(true, QString("Created folder: %1").arg(dirName));
    } else {
        emit operationError(QString("Failed to create folder: %1").arg(dirName));
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
    QString err;
    bool ok = performRename(oldPath, newName, &err);
    if (ok) {
        emit operationCompleted(true, QString("Renamed to: %1").arg(newName.trimmed()));
    } else {
        emit operationError(err.isEmpty() ? QString("Failed to rename item to: %1").arg(newName) : err);
    }
    return ok;
}
