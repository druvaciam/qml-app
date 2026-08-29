#include "PanelController.h"
#include "FileOperationsService.h"
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QSet>
#include <QTimer>

PanelController::PanelController(QObject *parent)
    : QObject(parent),
      m_model(new FileListModel(this))
{
    connect(m_model, &FileListModel::currentPathChanged, this, [this](const QString &path) {
        emit currentPathChanged(path);
        updateCurrentDriveInfo();
        emit statusChanged();
    });

    connect(m_model, &FileListModel::countChanged, this, [this]() {
        int cnt = m_model->count();
        if (cnt == 0) {
            if (m_currentIndex != -1) setCurrentIndex(-1);
        } else if (m_currentIndex >= cnt) {
            setCurrentIndex(cnt - 1);
        } else if (m_currentIndex < 0 && cnt > 0) {
            setCurrentIndex(0);
        }
        emit statusChanged();
    });
    connect(m_model, &FileListModel::selectionChanged, this, &PanelController::statusChanged);

    refreshDrives();
}

QString PanelController::currentPath() const
{
    return m_model ? m_model->currentPath() : QString();
}

void PanelController::setCurrentPath(const QString &path)
{
    navigateTo(path);
}

DriveInfo PanelController::currentDriveInfo() const
{
    return m_currentDriveInfo;
}

void PanelController::setIsActive(bool active)
{
    if (m_isActive != active) {
        m_isActive = active;
        emit isActiveChanged(m_isActive);
    }
}

void PanelController::setFilterText(const QString &text)
{
    if (m_filterText != text) {
        m_filterText = text;
        if (m_model) {
            m_model->setFilterPattern(m_filterText);
        }
        emit filterTextChanged(m_filterText);
    }
}

void PanelController::navigateTo(const QString &path)
{
    QString cleanPath = QDir::cleanPath(path);
    if (cleanPath.isEmpty()) {
        cleanPath = QDir::homePath();
    }

    QDir dir(cleanPath);
    if (!dir.exists()) {
        return;
    }

    if (!FileOperationsService::isSamePath(m_model->currentPath(), cleanPath)) {
        pushHistory(cleanPath);
        m_model->setCurrentPath(cleanPath);
    }
}

void PanelController::navigateUp()
{
    QString curr = currentPath();
    if (curr.isEmpty()) return;

    QDir dir(curr);
    if (!dir.isRoot()) {
        QString oldDirName = dir.dirName();
        QString parent = QDir::cleanPath(dir.filePath(QStringLiteral("..")));
        navigateTo(parent);
        setIsActive(true);
        if (m_model) {
            int idx = m_model->findItemIndex(oldDirName);
            if (idx >= 0) {
                setCurrentIndex(idx);
            } else {
                setCurrentIndex(0);
            }
        }
    }
}

void PanelController::navigateRoot()
{
    QString curr = currentPath();
    if (curr.isEmpty()) return;

    QStorageInfo storage(curr);
    if (storage.isValid()) {
        navigateTo(storage.rootPath());
    } else {
        navigateTo(QDir::rootPath());
    }
}

void PanelController::goBack()
{
    if (canGoBack()) {
        m_historyIndex--;
        QString path = m_history.at(m_historyIndex);
        m_model->setCurrentPath(path);
        emit historyChanged();
    }
}

void PanelController::goForward()
{
    if (canGoForward()) {
        m_historyIndex++;
        QString path = m_history.at(m_historyIndex);
        m_model->setCurrentPath(path);
        emit historyChanged();
    }
}

void PanelController::changeDrive(const QString &rootPath)
{
    navigateTo(rootPath);
}

void PanelController::refresh()
{
    if (m_model) {
        m_model->refresh();
    }
    // Only the current drive's free space, not a full QStorageInfo::mountedVolumes()
    // sweep. That sweep can block on a disconnected network drive, and refresh()
    // runs after every file operation, rename and context menu.
    updateCurrentDriveInfo();
}

void PanelController::refreshDrives()
{
    m_driveList = DriveInfo::getMountedDrives();
    emit driveListChanged();
    updateCurrentDriveInfo();
}

void PanelController::toggleShowHidden()
{
    if (m_model) {
        m_model->toggleShowHidden();
    }
}

void PanelController::openItem(int index)
{
    if (!m_model) return;

    setIsActive(true);

    QVariantMap item = m_model->get(index);
    if (item.isEmpty()) return;

    bool isParent = item["isParent"].toBool();
    if (isParent) {
        navigateUp();
        return;
    }

    bool isDir = item["isDir"].toBool();
    QString path = item["filePath"].toString();

    if (isDir) {
        navigateTo(path);
        setCurrentIndex(0);
    } else {
        emit fileActivated(path);
    }
}

void PanelController::setCurrentIndex(int idx)
{
    m_currentIndex = idx;
    emit currentIndexChanged(m_currentIndex);
}

QString PanelController::currentItemPath() const
{
    if (!m_model) return QString();
    QVariantMap item = m_model->get(m_currentIndex);
    if (!item.isEmpty() && !item["isParent"].toBool()) {
        return item["filePath"].toString();
    }
    return QString();
}

QStringList PanelController::getActiveOrSelectedPaths() const
{
    QStringList selected = getSelectedPaths();
    if (!selected.isEmpty()) {
        return selected;
    }

    QString current = currentItemPath();
    if (!current.isEmpty()) {
        return QStringList{current};
    }

    return QStringList();
}

QStringList PanelController::getSelectedPaths() const
{
    return m_model ? m_model->getSelectedPaths() : QStringList();
}

void PanelController::pushHistory(const QString &path)
{
    // If navigating to something new while not at the tip of history, prune forward history
    if (m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1) {
        m_history = m_history.mid(0, m_historyIndex + 1);
    }

    if (m_history.isEmpty() || m_history.last() != path) {
        m_history.append(path);
        m_historyIndex = m_history.size() - 1;
        emit historyChanged();
    }
}

void PanelController::updateCurrentDriveInfo()
{
    QString curr = currentPath();
    if (curr.isEmpty()) return;

    QStorageInfo storage(curr);
    if (storage.isValid()) {
        m_currentDriveInfo = DriveInfo(storage);
    } else {
        m_currentDriveInfo = DriveInfo();
    }
    emit currentDriveInfoChanged();
}

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>
#include <vector>

static IContextMenu2 *s_pMenu2 = nullptr;
static IContextMenu3 *s_pMenu3 = nullptr;

static LRESULT CALLBACK ShellMenuSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg) {
    case WM_INITMENUPOPUP:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
        if (s_pMenu2) {
            if (SUCCEEDED(s_pMenu2->HandleMenuMsg(uMsg, wParam, lParam))) {
                return 0;
            }
        }
        break;
    case WM_MENUCHAR:
        if (s_pMenu3) {
            LRESULT lres = 0;
            if (SUCCEEDED(s_pMenu3->HandleMenuMsg2(uMsg, wParam, lParam, &lres))) {
                return lres;
            }
        }
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}
#endif

void PanelController::showContextMenu(int globalX, int globalY, int index)
{
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        hwnd = GetActiveWindow();
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needUninit = SUCCEEDED(hr);

    IContextMenu *pContextMenu = nullptr;

    QStringList targetPaths;
    if (index >= 0 && m_model) {
        QVariantMap item = m_model->get(index);
        bool isParent = item["isParent"].toBool();
        if (!isParent) {
            QString filePath = item["filePath"].toString();
            QStringList selected = m_model->getSelectedPaths();
            if (selected.contains(filePath) && selected.size() > 1) {
                targetPaths = selected;
            } else if (!filePath.isEmpty()) {
                targetPaths.append(filePath);
            }
        }
    }

    if (targetPaths.isEmpty()) {
        // Folder background context menu (New, Properties, etc.)
        QString dirPath = currentPath();
        if (dirPath.isEmpty()) {
            if (needUninit) CoUninitialize();
            return;
        }

        std::wstring wDir = QDir::toNativeSeparators(QDir::cleanPath(dirPath)).toStdWString();
        PIDLIST_ABSOLUTE pidlDir = ILCreateFromPathW(wDir.c_str());
        if (pidlDir) {
            IShellFolder *pDesktop = nullptr;
            if (SUCCEEDED(SHGetDesktopFolder(&pDesktop))) {
                IShellFolder *pDirFolder = nullptr;
                if (SUCCEEDED(pDesktop->BindToObject(pidlDir, NULL, IID_IShellFolder, (void**)&pDirFolder))) {
                    pDirFolder->CreateViewObject(hwnd, IID_IContextMenu, (void**)&pContextMenu);
                    pDirFolder->Release();
                }
                pDesktop->Release();
            }
            ILFree(pidlDir);
        }
    } else {
        // Item(s) context menu (Open, Edit, Properties, etc.)
        std::vector<PIDLIST_ABSOLUTE> fullPidls;
        std::vector<LPCITEMIDLIST> childPidls;
        IShellFolder *pFolder = nullptr;

        for (const QString &path : targetPaths) {
            std::wstring wPath = QDir::toNativeSeparators(QDir::cleanPath(path)).toStdWString();
            PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(wPath.c_str());
            if (pidl) {
                fullPidls.push_back(pidl);
                if (!pFolder) {
                    LPCITEMIDLIST child = nullptr;
                    if (SUCCEEDED(SHBindToParent(pidl, IID_IShellFolder, (void**)&pFolder, &child))) {
                        childPidls.push_back(ILFindLastID(pidl));
                    }
                } else {
                    childPidls.push_back(ILFindLastID(pidl));
                }
            }
        }

        if (pFolder && !childPidls.empty()) {
            pFolder->GetUIObjectOf(hwnd, static_cast<UINT>(childPidls.size()), childPidls.data(), IID_IContextMenu, NULL, (void**)&pContextMenu);
            pFolder->Release();
        }

        for (auto p : fullPidls) {
            ILFree(p);
        }
    }

    // Remember what the folder held so a newly created item can be spotted below.
    const QStringList namesBefore = m_model ? m_model->fileNames() : QStringList();

    if (pContextMenu) {
        HMENU hMenu = CreatePopupMenu();
        if (hMenu) {
            UINT flags = CMF_NORMAL | CMF_EXPLORE;
            hr = pContextMenu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, flags);
            if (SUCCEEDED(hr)) {
                pContextMenu->QueryInterface(IID_IContextMenu2, (void**)&s_pMenu2);
                pContextMenu->QueryInterface(IID_IContextMenu3, (void**)&s_pMenu3);

                const UINT_PTR SUBCLASS_ID = 0x5C4E;
                if (hwnd) {
                    SetWindowSubclass(hwnd, ShellMenuSubclassProc, SUBCLASS_ID, 0);
                }

                POINT pt;
                if (GetCursorPos(&pt)) {
                    globalX = pt.x;
                    globalY = pt.y;
                }

                UINT cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTBUTTON, globalX, globalY, hwnd, NULL);

                if (hwnd) {
                    RemoveWindowSubclass(hwnd, ShellMenuSubclassProc, SUBCLASS_ID);
                }

                if (s_pMenu3) { s_pMenu3->Release(); s_pMenu3 = nullptr; }
                if (s_pMenu2) { s_pMenu2->Release(); s_pMenu2 = nullptr; }

                if (cmd > 0) {
                    CMINVOKECOMMANDINFOEX cmi = {0};
                    cmi.cbSize = sizeof(cmi);
                    cmi.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                    cmi.hwnd = hwnd;
                    cmi.lpVerb = (LPCSTR)MAKEINTRESOURCE(cmd - 1);
                    cmi.lpVerbW = (LPCWSTR)MAKEINTRESOURCEW(cmd - 1);
                    cmi.nShow = SW_SHOWNORMAL;
                    cmi.ptInvoke.x = globalX;
                    cmi.ptInvoke.y = globalY;
                    pContextMenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&cmi);
                }
            }
            DestroyMenu(hMenu);
        }
        pContextMenu->Release();
    }

    if (needUninit) {
        CoUninitialize();
    }

    refresh();
    trySelectNewItem(namesBefore, 3);
#else
    Q_UNUSED(globalX);
    Q_UNUSED(globalY);
    Q_UNUSED(index);
#endif
}

void PanelController::trySelectNewItem(const QStringList &namesBefore, int attemptsLeft)
{
    if (!m_model) return;

    const QSet<QString> before(namesBefore.begin(), namesBefore.end());
    const QStringList now = m_model->fileNames();

    int newIndex = -1;
    int newCount = 0;
    for (int i = 0; i < now.size(); ++i) {
        if (!before.contains(now.at(i))) {
            ++newCount;
            newIndex = m_model->findItemIndex(now.at(i));
        }
    }

    // Exactly one new item means a "New >" command. Several means a paste or an
    // extract, where renaming one of them would be wrong.
    if (newCount == 1 && newIndex >= 0) {
        setCurrentIndex(newIndex);
        emit inlineRenameRequested(newIndex);
        return;
    }

    // The shell may return before the file is on disk, and our own watcher
    // reload is debounced, so look again a few times before giving up. Only
    // retry when the listing is completely unchanged: if items disappeared, the
    // command was a delete or a move and there is nothing to wait for.
    const bool listingUnchanged = (newCount == 0 && now.size() == namesBefore.size());
    if (listingUnchanged && attemptsLeft > 0) {
        QTimer::singleShot(250, this, [this, namesBefore, attemptsLeft]() {
            refresh();
            trySelectNewItem(namesBefore, attemptsLeft - 1);
        });
    }
}

bool PanelController::renameItem(const QString &oldPath, const QString &newName)
{
    bool ok = FileOperationsService::performRename(oldPath, newName);
    if (ok) {
        refresh();
    }
    return ok;
}

QStringList PanelController::getDragPaths(int index) const
{
    QString itemPath;
    if (m_model && index >= 0 && index < m_model->count()) {
        itemPath = m_model->get(index).value(QStringLiteral("filePath")).toString();
    }
    QStringList selected = getSelectedPaths();
    if (!itemPath.isEmpty() && selected.contains(itemPath)) {
        return selected;
    }
    if (!itemPath.isEmpty()) {
        return {itemPath};
    }
    return selected;
}

QList<QUrl> PanelController::getDragUrls(int index) const
{
    QStringList paths = getDragPaths(index);
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString &p : paths) {
        urls.append(QUrl::fromLocalFile(p));
    }
    return urls;
}

