/**
 * @file FileListModel.h
 * @brief QAbstractListModel delivering sorted, filtered directory contents to QML.
 *
 * ARCHITECTURAL ROLE:
 * FileListModel is the data source owned by each PanelController.
 * It reads the file system for a given directory path and formats items for UI display:
 *   - Formatted item metadata (icons, human-readable file sizes, modification dates).
 *   - In-memory selection tracking (single select, multi-select, select all, invert).
 *   - Sorting by Name, Extension, Size, or Date in ascending/descending order.
 *   - Quick filtering (wildcards/substring search).
 *   - Automatic file system monitoring via QFileSystemWatcher for live directory updates.
 */

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QtQml/qqmlregistration.h>

struct FileItem {
    QString name;
    QString fullPath;
    bool isDir = false;
    bool isParent = false;
    bool isHidden = false;
    bool isExecutable = false;
    qint64 size = 0;
    QString formattedSize;
    QString extension;
    QDateTime lastModified;
    QString formattedModified;
    QString fileType;
    QString permissions;
    bool isSelected = false;
};

class FileListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString currentPath READ currentPath WRITE setCurrentPath NOTIFY currentPathChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSizeFormatted READ selectedSizeFormatted NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectedSizeBytes READ selectedSizeBytes NOTIFY selectionChanged)
    Q_PROPERTY(int sortColumn READ sortColumn WRITE setSortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QString filterPattern READ filterPattern WRITE setFilterPattern NOTIFY filterPatternChanged)

public:
    enum FileRoles {
        NameRole = Qt::UserRole + 1,
        FullPathRole,
        IsDirRole,
        IsParentRole,
        IsHiddenRole,
        IsExecutableRole,
        SizeRole,
        FormattedSizeRole,
        ExtensionRole,
        ModifiedRole,
        FormattedModifiedRole,
        FileTypeRole,
        PermissionsRole,
        IsSelectedRole
    };
    Q_ENUM(FileRoles)

    enum SortColumn {
        SortByName = 0,
        SortByExt,
        SortBySize,
        SortByDate
    };
    Q_ENUM(SortColumn)

    explicit FileListModel(QObject *parent = nullptr);
    ~FileListModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QHash<int, QByteArray> roleNames() const override;

    QString currentPath() const { return m_currentPath; }
    void setCurrentPath(const QString &path);

    int count() const { return static_cast<int>(m_items.size()); }
    int selectedCount() const { return m_selectedCount; }
    qint64 selectedSizeBytes() const { return m_selectedSizeBytes; }
    QString selectedSizeFormatted() const;

    int sortColumn() const { return m_sortColumn; }
    void setSortColumn(int col);

    bool sortAscending() const { return m_sortAscending; }
    void setSortAscending(bool ascending);

    bool showHidden() const { return m_showHidden; }
    Q_INVOKABLE void setShowHidden(bool show);
    Q_INVOKABLE void toggleShowHidden();

    QString filterPattern() const { return m_filterPattern; }
    void setFilterPattern(const QString &pattern);

    // QML Invokables for interaction
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void toggleSort(int column);
    Q_INVOKABLE void toggleSelection(int index);
    Q_INVOKABLE void setRowSelected(int index, bool selected);
    Q_INVOKABLE void selectOnly(int index);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deselectAll();
    Q_INVOKABLE void invertSelection();
    Q_INVOKABLE QStringList getSelectedPaths() const;
    Q_INVOKABLE QString getSelectedSummary() const;
    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE int findItemIndex(const QString &fileName) const;

signals:
    void currentPathChanged(const QString &path);
    void countChanged();
    void selectionChanged();
    void sortChanged();
    void showHiddenChanged();
    void filterPatternChanged();
    void directoryLoadError(const QString &error);
    void beforeDirectoryReset(bool isNewPath);
    void directoryReset(bool isNewPath);

private slots:
    void onDirectoryChanged(const QString &path);

private:
    void loadDirectory();
    void sortItems();
    void sortInternal();
    void updateSelectionStats();
    static QString detectFileType(const QFileInfo &info);
    static QString formatSize(qint64 bytes, bool isDir);
    static QString formatPermissions(const QFileInfo &info);

    QString m_currentPath;
    QList<FileItem> m_items;
    QFileSystemWatcher m_watcher;

    int m_sortColumn = SortByName;
    bool m_sortAscending = true;
    bool m_showHidden = false;
    bool m_isNewPathNavigation = false;
    QString m_filterPattern;

    int m_selectedCount = 0;
    qint64 m_selectedSizeBytes = 0;
};
