#include "AppController.h"
#include <QDir>
#include <QSettings>
#include <QGuiApplication>
#include <QScreen>
#include <QQuickWindow>
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>

class TrayEventFilter : public QAbstractNativeEventFilter
{
public:
    TrayEventFilter(HWND hwnd, QQuickWindow *window) : m_hwnd(hwnd), m_window(window) {}

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        Q_UNUSED(result);
        if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
            MSG *msg = static_cast<MSG *>(message);
            if (msg->hwnd == m_hwnd && msg->message == (WM_APP + 101)) {
                if (msg->lParam == WM_LBUTTONUP || msg->lParam == WM_LBUTTONDBLCLK) {
                    if (m_window) {
                        if (m_window->visibility() == QWindow::Minimized) m_window->showNormal();
                        else m_window->show();
                        m_window->raise();
                        m_window->requestActivate();
                    }
                    return true;
                } else if (msg->lParam == WM_RBUTTONUP) {
                    POINT pt;
                    GetCursorPos(&pt);
                    HMENU hMenu = CreatePopupMenu();
                    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, 1, L"Open QML Commander");
                    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
                    InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING, 2, L"Exit");

                    SetForegroundWindow(m_hwnd);
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
                    DestroyMenu(hMenu);

                    if (cmd == 1) {
                        if (m_window) {
                            if (m_window->visibility() == QWindow::Minimized) m_window->showNormal();
                            else m_window->show();
                            m_window->raise();
                            m_window->requestActivate();
                        }
                    } else if (cmd == 2) {
                        if (m_window) m_window->close();
                    }
                    return true;
                }
            }
        }
        return false;
    }

private:
    HWND m_hwnd = nullptr;
    QQuickWindow *m_window = nullptr;
};
#endif

AppController::AppController(QObject *parent)
    : QObject(parent),
      m_leftPanel(new PanelController(this)),
      m_rightPanel(new PanelController(this)),
      m_fileOps(new FileOperationsService(this)),
      m_preview(new FilePreviewService(this))
{
    // Connect hidden file changes to auto-save session
    if (m_leftPanel && m_leftPanel->model()) {
        connect(m_leftPanel->model(), &FileListModel::showHiddenChanged, this, &AppController::saveSession);
    }
    if (m_rightPanel && m_rightPanel->model()) {
        connect(m_rightPanel->model(), &FileListModel::showHiddenChanged, this, &AppController::saveSession);
    }

    // Load and restore last session paths and settings
    loadSession();

    connect(m_leftPanel, &PanelController::fileActivated, this, &AppController::requestPreviewFile);
    connect(m_rightPanel, &PanelController::fileActivated, this, &AppController::requestPreviewFile);

    connect(m_leftPanel, &PanelController::currentPathChanged, this, &AppController::saveSession);
    connect(m_rightPanel, &PanelController::currentPathChanged, this, &AppController::saveSession);

    connect(m_leftPanel, &PanelController::isActiveChanged, this, [this](bool active) {
        if (active && m_activePanelIndex != 0) {
            setActivePanelIndex(0);
        }
    });

    connect(m_rightPanel, &PanelController::isActiveChanged, this, [this](bool active) {
        if (active && m_activePanelIndex != 1) {
            setActivePanelIndex(1);
        }
    });

    connect(m_fileOps, &FileOperationsService::operationCompleted,
            this, &AppController::onFileOperationCompleted);
}

AppController::~AppController()
{
    saveSession();
#ifdef Q_OS_WIN
    if (m_trayInstalled && m_windowHwnd) {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = reinterpret_cast<HWND>(m_windowHwnd);
        nid.uID = 1001;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        m_trayInstalled = false;
    }
    if (m_eventFilter) {
        QCoreApplication::instance()->removeNativeEventFilter(m_eventFilter);
        delete m_eventFilter;
        m_eventFilter = nullptr;
    }
#endif
}

void AppController::toggleHiddenFiles()
{
    if (activePanel()) {
        activePanel()->toggleShowHidden();
    }
}

void AppController::loadSession()
{
    QSettings settings;
    QString savedLeft = settings.value(QStringLiteral("session/leftPanelPath")).toString();
    QString savedRight = settings.value(QStringLiteral("session/rightPanelPath")).toString();
    int savedActive = settings.value(QStringLiteral("session/activePanelIndex"), 0).toInt();
    bool leftHidden = settings.value(QStringLiteral("session/leftShowHidden"), false).toBool();
    bool rightHidden = settings.value(QStringLiteral("session/rightShowHidden"), false).toBool();

    // Restore showHidden state on models before loading directories
    if (m_leftPanel && m_leftPanel->model()) {
        m_leftPanel->model()->setShowHidden(leftHidden);
    }
    if (m_rightPanel && m_rightPanel->model()) {
        m_rightPanel->model()->setShowHidden(rightHidden);
    }

    QString homePath = QDir::homePath();

    // Restore Left Panel
    if (!savedLeft.isEmpty() && QDir(savedLeft).exists()) {
        m_leftPanel->navigateTo(savedLeft);
    } else {
        m_leftPanel->navigateTo(homePath);
    }

    // Restore Right Panel
    if (!savedRight.isEmpty() && QDir(savedRight).exists()) {
        m_rightPanel->navigateTo(savedRight);
    } else {
        auto drives = m_rightPanel->driveList();
        if (drives.size() > 1) {
            m_rightPanel->navigateTo(drives.at(1).rootPath());
        } else {
            m_rightPanel->navigateTo(homePath);
        }
    }

    setActivePanelIndex(savedActive == 1 ? 1 : 0);
}

void AppController::saveSession()
{
    QSettings settings;
    if (m_leftPanel && !m_leftPanel->currentPath().isEmpty()) {
        settings.setValue(QStringLiteral("session/leftPanelPath"), m_leftPanel->currentPath());
    }
    if (m_rightPanel && !m_rightPanel->currentPath().isEmpty()) {
        settings.setValue(QStringLiteral("session/rightPanelPath"), m_rightPanel->currentPath());
    }
    settings.setValue(QStringLiteral("session/activePanelIndex"), m_activePanelIndex);

    if (m_leftPanel && m_leftPanel->model()) {
        settings.setValue(QStringLiteral("session/leftShowHidden"), m_leftPanel->model()->showHidden());
    }
    if (m_rightPanel && m_rightPanel->model()) {
        settings.setValue(QStringLiteral("session/rightShowHidden"), m_rightPanel->model()->showHidden());
    }
}

void AppController::restoreWindowGeometry(QQuickWindow *window)
{
    if (!window) return;

    QSettings settings;
    int w = settings.value(QStringLiteral("window/width"), 1280).toInt();
    int h = settings.value(QStringLiteral("window/height"), 780).toInt();
    w = std::max(850, w);
    h = std::max(520, h);

    bool hasPos = settings.contains(QStringLiteral("window/x")) && settings.contains(QStringLiteral("window/y"));
    int x = settings.value(QStringLiteral("window/x"), 100).toInt();
    int y = settings.value(QStringLiteral("window/y"), 100).toInt();
    bool isMax = settings.value(QStringLiteral("window/isMaximized"), false).toBool();

    // Check if (x, y) is on a valid connected screen
    bool validPos = false;
    if (hasPos) {
        const auto screens = QGuiApplication::screens();
        for (const QScreen *screen : screens) {
            if (screen->geometry().contains(x + 50, y + 50)) {
                validPos = true;
                break;
            }
        }
    }

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd) {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(hwnd, &wp)) {
            if (validPos) {
                wp.rcNormalPosition.left = x;
                wp.rcNormalPosition.top = y;
                wp.rcNormalPosition.right = x + w;
                wp.rcNormalPosition.bottom = y + h;
            } else {
                int curLeft = wp.rcNormalPosition.left;
                int curTop = wp.rcNormalPosition.top;
                wp.rcNormalPosition.right = curLeft + w;
                wp.rcNormalPosition.bottom = curTop + h;
            }

            if (isMax) {
                wp.showCmd = SW_SHOWMAXIMIZED;
            } else {
                wp.showCmd = SW_SHOWNORMAL;
            }
            SetWindowPlacement(hwnd, &wp);
            return;
        }
    }
#endif

    window->resize(w, h);
    if (validPos) {
        window->setPosition(x, y);
    }
    if (isMax) {
        window->showMaximized();
    } else {
        window->showNormal();
    }
}

void AppController::saveWindowGeometry(QQuickWindow *window)
{
    if (!window) return;

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd) {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(hwnd, &wp)) {
            int normalWidth = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
            int normalHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
            int normalX = wp.rcNormalPosition.left;
            int normalY = wp.rcNormalPosition.top;
            bool isMax = (wp.showCmd == SW_SHOWMAXIMIZED) || (window->visibility() == QWindow::Maximized);

            QSettings settings;
            if (normalWidth >= 850) {
                settings.setValue(QStringLiteral("window/width"), normalWidth);
            }
            if (normalHeight >= 520) {
                settings.setValue(QStringLiteral("window/height"), normalHeight);
            }
            settings.setValue(QStringLiteral("window/x"), normalX);
            settings.setValue(QStringLiteral("window/y"), normalY);
            settings.setValue(QStringLiteral("window/isMaximized"), isMax);
            return;
        }
    }
#endif

    QSettings settings;
    bool isMax = (window->visibility() == QWindow::Maximized);
    settings.setValue(QStringLiteral("window/isMaximized"), isMax);
    if (!isMax) {
        if (window->width() >= 850) settings.setValue(QStringLiteral("window/width"), window->width());
        if (window->height() >= 520) settings.setValue(QStringLiteral("window/height"), window->height());
        settings.setValue(QStringLiteral("window/x"), window->x());
        settings.setValue(QStringLiteral("window/y"), window->y());
    }
}

void AppController::setupTrayIcon(QQuickWindow *window)
{
#ifdef Q_OS_WIN
    if (m_trayInstalled || !window) return;
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return;

    m_windowHwnd = hwnd;

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1001;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP + 101;

    // Load custom application icon directly from compiled executable resources
    HICON hIcon = (HICON)LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!hIcon) {
        hIcon = (HICON)LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    }
    nid.hIcon = hIcon;
    wcscpy_s(nid.szTip, L"QML Commander - Dual-Pane File Manager");

    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        m_trayInstalled = true;
        if (!m_eventFilter) {
            m_eventFilter = new TrayEventFilter(hwnd, window);
            QCoreApplication::instance()->installNativeEventFilter(m_eventFilter);
        }
    }
#else
    Q_UNUSED(window);
#endif
}

void AppController::setActivePanelIndex(int index)
{
    int nextIndex = (index == 1) ? 1 : 0;
    bool changed = (m_activePanelIndex != nextIndex);
    m_activePanelIndex = nextIndex;
    m_leftPanel->setIsActive(m_activePanelIndex == 0);
    m_rightPanel->setIsActive(m_activePanelIndex == 1);
    if (changed) {
        emit activePanelIndexChanged(m_activePanelIndex);
        saveSession();
    }
}

void AppController::toggleActivePanel()
{
    setActivePanelIndex(m_activePanelIndex == 0 ? 1 : 0);
}

void AppController::swapPanes()
{
    QString leftPath = m_leftPanel->currentPath();
    QString rightPath = m_rightPanel->currentPath();

    m_leftPanel->navigateTo(rightPath);
    m_rightPanel->navigateTo(leftPath);
}

void AppController::equalizePanes()
{
    QString activePath = activePanel()->currentPath();
    targetPanel()->navigateTo(activePath);
}

QStringList AppController::getActiveOrSelectedPaths() const
{
    if (!activePanel()) return QStringList();
    return activePanel()->getActiveOrSelectedPaths();
}

void AppController::copySelected(const QStringList &customPaths, const QString &customDestination)
{
    QStringList targets = !customPaths.isEmpty() ? customPaths : getActiveOrSelectedPaths();
    if (targets.isEmpty()) {
        emit showMessageRequested(QStringLiteral("Copy"), QStringLiteral("No files or folders selected to copy."));
        return;
    }

    QString dst = !customDestination.isEmpty() ? customDestination : targetPanel()->currentPath();
    if (dst.isEmpty()) {
        emit showMessageRequested(QStringLiteral("Copy"), QStringLiteral("Destination folder is not specified."));
        return;
    }

    m_fileOps->copyItems(targets, dst);
}

void AppController::moveSelected(const QStringList &customPaths, const QString &customDestination)
{
    QStringList targets = !customPaths.isEmpty() ? customPaths : getActiveOrSelectedPaths();
    if (targets.isEmpty()) {
        emit showMessageRequested(QStringLiteral("Move"), QStringLiteral("No files or folders selected to move."));
        return;
    }

    QString dst = !customDestination.isEmpty() ? customDestination : targetPanel()->currentPath();
    if (dst.isEmpty()) {
        emit showMessageRequested(QStringLiteral("Move"), QStringLiteral("Destination folder is not specified."));
        return;
    }

    if (QDir::cleanPath(dst) == QDir::cleanPath(activePanel()->currentPath())) {
        emit showMessageRequested(QStringLiteral("Move"), QStringLiteral("Source and target folders are identical. Use F2 to rename."));
        return;
    }

    m_fileOps->moveItems(targets, dst);
}

void AppController::deleteSelected(const QStringList &customPaths, bool permanent)
{
    QStringList targets = !customPaths.isEmpty() ? customPaths : getActiveOrSelectedPaths();
    if (targets.isEmpty()) {
        emit showMessageRequested(QStringLiteral("Delete"), QStringLiteral("No files or folders selected to delete."));
        return;
    }

    m_fileOps->deleteItems(targets, permanent);
}

bool AppController::createFolder(const QString &folderName)
{
    QString activePath = activePanel()->currentPath();
    bool ok = m_fileOps->createDirectory(activePath, folderName);
    if (ok) {
        activePanel()->refresh();
    }
    return ok;
}

bool AppController::renameActiveItem(const QString &oldPath, const QString &newName)
{
    bool ok = m_fileOps->renameItem(oldPath, newName);
    if (ok) {
        activePanel()->refresh();
        targetPanel()->refresh();
    }
    return ok;
}

void AppController::refreshAll()
{
    m_leftPanel->refresh();
    m_rightPanel->refresh();
}

void AppController::openTerminalInActivePanel()
{
    QString activePath = activePanel()->currentPath();
    m_preview->openInTerminal(activePath);
}

void AppController::onFileOperationCompleted(bool success, const QString &message)
{
    refreshAll();
    if (!success && !message.isEmpty()) {
        emit showMessageRequested(QStringLiteral("Operation Failed"), message);
    }
}

void AppController::setDraggedPaths(const QStringList &paths)
{
    if (m_draggedPaths != paths) {
        m_draggedPaths = paths;
        emit draggedPathsChanged();
    }
}

void AppController::clearDraggedPaths()
{
    setDraggedPaths(QStringList());
}

QStringList AppController::urlsToPaths(const QList<QUrl> &urls) const
{
    QStringList paths;
    for (const QUrl &url : urls) {
        QString local = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (!local.isEmpty()) {
            paths.append(QDir::toNativeSeparators(local));
        }
    }
    return paths;
}
