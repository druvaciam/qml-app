/**
 * @file AppController.h
 * @brief Top-level application coordinator managing dual panels and commander
 * workflows.
 *
 * ARCHITECTURAL ROLE:
 * AppController is the root orchestrator of the commander application:
 *   - Owns and manages the two PanelController instances (leftPanel and
 * rightPanel).
 *   - Tracks which panel is the source of user commands (activePanel) and which
 * is the destination for dual-pane file operations (targetPanel).
 *   - Owns the centralized FileOperationsService (background worker) and
 * FilePreviewService.
 *   - Translates high-level commander hotkeys and menu actions:
 *       * F5 (Copy): Reads selected items from activePanel, invokes
 * FileOperationsService::copyItems to targetPanel path.
 *       * F6 (Move): Reads selected items from activePanel, invokes
 * FileOperationsService::moveItems to targetPanel path.
 *       * F8 (Delete): Reads selected items from activePanel, invokes
 * FileOperationsService::deleteItems.
 *       * Tab / Ctrl+U: Toggles or swaps active and target panels.
 *       * Session Save/Load: Persists panel directories across application
 * runs.
 */

#pragma once

#include "FileOperationsService.h"
#include "FilePreviewService.h"
#include "PanelController.h"
#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <QQuickWindow>

class AppController : public QObject {
  Q_OBJECT
  QML_ELEMENT

  // Dual-Pane References
  Q_PROPERTY(PanelController *leftPanel READ leftPanel CONSTANT)
  Q_PROPERTY(PanelController *rightPanel READ rightPanel CONSTANT)
  Q_PROPERTY(int activePanelIndex READ activePanelIndex WRITE
                 setActivePanelIndex NOTIFY activePanelIndexChanged)
  Q_PROPERTY(PanelController *activePanel READ activePanel NOTIFY
                 activePanelIndexChanged)
  Q_PROPERTY(PanelController *targetPanel READ targetPanel NOTIFY
                 activePanelIndexChanged)

  // Shared Sub-services
  Q_PROPERTY(FileOperationsService *fileOps READ fileOps CONSTANT)
  Q_PROPERTY(FilePreviewService *preview READ preview CONSTANT)

  // Drag-and-drop transfer state
  Q_PROPERTY(QStringList draggedPaths READ draggedPaths WRITE setDraggedPaths
                 NOTIFY draggedPathsChanged)

public:
  explicit AppController(QObject *parent = nullptr);
  ~AppController() override;

  Q_INVOKABLE void toggleHiddenFiles();

  PanelController *leftPanel() const { return m_leftPanel; }
  PanelController *rightPanel() const { return m_rightPanel; }

  int activePanelIndex() const { return m_activePanelIndex; }
  void setActivePanelIndex(int index);

  PanelController *activePanel() const {
    return (m_activePanelIndex == 0) ? m_leftPanel : m_rightPanel;
  }
  PanelController *targetPanel() const {
    return (m_activePanelIndex == 0) ? m_rightPanel : m_leftPanel;
  }

  FileOperationsService *fileOps() const { return m_fileOps; }
  FilePreviewService *preview() const { return m_preview; }

  QStringList draggedPaths() const { return m_draggedPaths; }
  void setDraggedPaths(const QStringList &paths);
  Q_INVOKABLE void clearDraggedPaths();
  Q_INVOKABLE QStringList urlsToPaths(const QList<QUrl> &urls) const;

  // --- Dual-Pane Coordination ---
  Q_INVOKABLE void toggleActivePanel();
  Q_INVOKABLE void swapPanes();
  Q_INVOKABLE void equalizePanes();

  // --- Commander Workflow Actions (Delegating to FileOperationsService) ---
  /**
   * @brief Copy selected items from activePanel into targetPanel's directory.
   */
  Q_INVOKABLE void copySelected(const QStringList &customPaths = QStringList(),
                                const QString &customDestination = QString());

  /**
   * @brief Move selected items from activePanel into targetPanel's directory.
   */
  Q_INVOKABLE void moveSelected(const QStringList &customPaths = QStringList(),
                                const QString &customDestination = QString());

  /**
   * @brief Delete selected items from activePanel (Recycle Bin or permanent).
   */
  Q_INVOKABLE void
  deleteSelected(const QStringList &customPaths = QStringList(),
                 bool permanent = false);

  /**
   * @brief Get selected paths from activePanel, or currently focused item if
   * none selected.
   */
  Q_INVOKABLE QStringList getActiveOrSelectedPaths() const;

  /**
   * @brief Create a new folder inside activePanel's current directory.
   */
  Q_INVOKABLE bool createFolder(const QString &folderName);

  /**
   * @brief Rename an item in activePanel via FileOperationsService.
   */
  Q_INVOKABLE bool renameActiveItem(const QString &oldPath,
                                    const QString &newName);

  /**
   * @brief Refresh file lists and drive statistics in both panels.
   */
  Q_INVOKABLE void refreshAll();

  /**
   * @brief Open a system terminal (PowerShell / CMD) inside activePanel's
   * current directory.
   */
  Q_INVOKABLE void openTerminalInActivePanel();

  // --- Persistence ---
  Q_INVOKABLE void saveSession();
  Q_INVOKABLE void loadSession();
  Q_INVOKABLE void saveWindowGeometry(QQuickWindow *window);
  Q_INVOKABLE void restoreWindowGeometry(QQuickWindow *window);
  Q_INVOKABLE void setupTrayIcon(QQuickWindow *window);

signals:
  void activePanelIndexChanged(int index);
  void showMessageRequested(const QString &title, const QString &message);
  void requestPreviewFile(const QString &filePath);
  void draggedPathsChanged();

private:
  void onFileOperationCompleted(bool success, const QString &message);
  PanelController *m_leftPanel = nullptr;
  PanelController *m_rightPanel = nullptr;
#ifdef Q_OS_WIN
  void *m_windowHwnd = nullptr;
  bool m_trayInstalled = false;
  class TrayEventFilter *m_eventFilter = nullptr;
#endif
  FileOperationsService *m_fileOps = nullptr;
  FilePreviewService *m_preview = nullptr;
  int m_activePanelIndex = 0;
  QStringList m_draggedPaths;
};
