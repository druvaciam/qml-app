/**
 * @file PanelController.h
 * @brief Controller managing the UI state, navigation, and user actions of a single file panel.
 *
 * ARCHITECTURAL ROLE:
 * In a dual-pane commander (left and right panes), each pane is represented by an
 * instance of PanelController. PanelController maintains the interactive state of that pane:
 *   - Current directory path, breadcrumbs, and drive selection.
 *   - Navigation history (back, forward, up, root).
 *   - Focus and active pane tracking (isActive, currentIndex).
 *   - Quick search filtering (filterText).
 *   - UI-bound item actions: opening folders/files, inline renaming, and Windows context menu.
 *
 * RELATIONSHIP TO FileOperationsService & FileListModel:
 *   - FileListModel is OWNED by PanelController: provides the raw sorted/filtered list
 *     items and selection state for this pane's QML ListView.
 *   - FileOperationsService is the BACKEND WORKER: PanelController does NOT perform
 *     asynchronous disk copy/move/delete jobs directly. When an action occurs (e.g. rename),
 *     PanelController invokes FileOperationsService and then calls refresh() on its model
 *     to update the view.
 *   - AppController is the PARENT COORDINATOR: AppController manages the two PanelController
 *     instances (left and right), routes global hotkeys (F5, F6, F8, Tab), and coordinates
 *     cross-panel operations (e.g. Copy from active panel to target panel).
 */

#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include "FileListModel.h"
#include "DriveInfo.h"

class PanelController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // Model & Navigation Properties
    Q_PROPERTY(FileListModel* model READ model CONSTANT)
    Q_PROPERTY(QString currentPath READ currentPath WRITE setCurrentPath NOTIFY currentPathChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(QList<DriveInfo> driveList READ driveList NOTIFY driveListChanged)
    Q_PROPERTY(DriveInfo currentDriveInfo READ currentDriveInfo NOTIFY currentDriveInfoChanged)

    // Selection & Item Statistics
    Q_PROPERTY(int totalItemsCount READ totalItemsCount NOTIFY statusChanged)
    Q_PROPERTY(int selectedItemsCount READ selectedItemsCount NOTIFY statusChanged)
    Q_PROPERTY(QString selectedSizeFormatted READ selectedSizeFormatted NOTIFY statusChanged)

    // Interactive UI State
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(bool isActive READ isActive WRITE setIsActive NOTIFY isActiveChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

public:
    explicit PanelController(QObject *parent = nullptr);
    ~PanelController() override = default;

    /// The list model containing the files and directories in currentPath.
    FileListModel* model() const { return m_model; }

    /// Current working directory path for this panel.
    QString currentPath() const;
    void setCurrentPath(const QString &path);

    bool canGoBack() const { return m_historyIndex > 0; }
    bool canGoForward() const { return m_historyIndex < m_history.size() - 1; }

    QList<DriveInfo> driveList() const { return m_driveList; }
    DriveInfo currentDriveInfo() const;

    int totalItemsCount() const { return m_model ? m_model->count() : 0; }
    int selectedItemsCount() const { return m_model ? m_model->selectedCount() : 0; }
    QString selectedSizeFormatted() const { return m_model ? m_model->selectedSizeFormatted() : QString(); }

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int idx);

    bool isActive() const { return m_isActive; }
    Q_INVOKABLE void setIsActive(bool active);
    Q_INVOKABLE void activate() { setIsActive(true); }

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &text);

    // --- Navigation Methods ---
    Q_INVOKABLE void navigateTo(const QString &path);
    Q_INVOKABLE void navigateUp();
    Q_INVOKABLE void navigateRoot();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void changeDrive(const QString &rootPath);

    // --- View Updates ---
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshDrives();
    Q_INVOKABLE void toggleShowHidden();

    // --- Item Interactions ---
    /**
     * @brief Open item at index: enters subdirectory or launches file via system default app.
     */
    Q_INVOKABLE void openItem(int index);

    /**
     * @brief Get absolute paths of all selected items in this panel.
     */
    Q_INVOKABLE QStringList getSelectedPaths() const;

    /**
     * @brief Get absolute path of the currently highlighted item.
     */
    Q_INVOKABLE QString currentItemPath() const;

    /**
     * @brief Return selected paths if any, or current item path if none selected.
     */
    Q_INVOKABLE QStringList getActiveOrSelectedPaths() const;

    /**
     * @brief Display the native Windows Shell context menu (IContextMenu) at screen coordinates.
     * @param globalX Screen X coordinate (pixels)
     * @param globalY Screen Y coordinate (pixels)
     * @param index Item index, or -1 for empty space (folder background menu)
     */
    Q_INVOKABLE void showContextMenu(int globalX, int globalY, int index = -1);

    /**
     * @brief Rename an item in this panel, delegating to FileOperationsService and refreshing.
     */
    Q_INVOKABLE bool renameItem(const QString &oldPath, const QString &newName);

    /**
     * @brief Return list of file paths to drag for item at index (or all selected items if index is selected).
     */
    Q_INVOKABLE QStringList getDragPaths(int index) const;

    /**
     * @brief Return list of file QUrls to drag for item at index (compatible with mimeData text/uri-list).
     */
    Q_INVOKABLE QList<QUrl> getDragUrls(int index) const;

signals:
    void currentPathChanged(const QString &path);
    void historyChanged();
    void driveListChanged();
    void currentDriveInfoChanged();
    void statusChanged();
    void currentIndexChanged(int index);
    void isActiveChanged(bool active);
    void filterTextChanged(const QString &filter);
    void fileActivated(const QString &filePath);

private:
    void pushHistory(const QString &path);
    void updateCurrentDriveInfo();

    FileListModel *m_model = nullptr;
    QStringList m_history;
    int m_historyIndex = -1;
    int m_currentIndex = 0;
    QList<DriveInfo> m_driveList;
    DriveInfo m_currentDriveInfo;
    bool m_isActive = false;
    QString m_filterText;
};
