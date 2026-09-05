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
#include <QFutureWatcher>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QSet>
#include <QRegularExpression>
#include <QtQml/qqmlregistration.h>

struct FileItem {
    QString name;
    QString fullPath;
    bool isDir = false;
    bool isParent = false;
    /// The name without its extension. Sorting compares this rather than the
    /// whole file name - see compareByName in the .cpp.
    QString baseName;
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
    /// True while the folder is being read on a worker thread. The rows are
    /// empty during that window, so the panel can say so rather than looking
    /// like an empty folder.
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
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
        IsSelectedRole,
        /// True for a row that has changed since the application started,
        /// or that one of its own copy, move or new-folder operations
        /// produced. Not stored on the row - worked out when asked.
        IsRecentRole
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

    /// True for the scratch files a copy writes beside its destination. They
    /// are ours, not the user's, and are never worth showing.
    static bool isCopyScratchFile(const QString &fileName);

    /// Rows the user can actually act on. `count` is the row count, which the
    /// view needs, but it includes the ".." row - that is navigation, not an
    /// item, and it cannot be selected. The status bar was reporting it and so
    /// read one too many in every folder below the root.
    int fileItemsCount() const
    {
        const int rows = static_cast<int>(m_items.size());
        return (rows > 0 && m_items.first().isParent) ? rows - 1 : rows;
    }
    int selectedCount() const { return m_selectedCount; }
    qint64 selectedSizeBytes() const { return m_selectedSizeBytes; }
    QString selectedSizeFormatted() const;

    int sortColumn() const { return m_sortColumn; }
    void setSortColumn(int col);

    bool sortAscending() const { return m_sortAscending; }
    void setSortAscending(bool ascending);

    bool showHidden() const { return m_showHidden; }
    bool isLoading() const { return m_isLoading; }
    Q_INVOKABLE void setShowHidden(bool show);
    Q_INVOKABLE void toggleShowHidden();

    QString filterPattern() const { return m_filterPattern; }
    Q_INVOKABLE void setFilterPattern(const QString &pattern);
    /// Drops the filter without re-reading the folder. Used when leaving a
    /// directory: going through setFilterPattern() there would reload the
    /// folder being left behind purely to throw the result away, which is a
    /// visible stall in a large one.
    void clearFilterNoReload();

    // QML Invokables for interaction
    Q_INVOKABLE void refresh();

    /**
     * @brief Re-read one file's size, date and permissions in place.
     *
     * Saving a file changes exactly one row. A full refresh() re-stats every
     * entry in the folder on the GUI thread, which visibly freezes the window
     * on a large directory (see the async-load finding). This touches one row.
     */
    Q_INVOKABLE void refreshItem(const QString &filePath);

    /// Applies a change the app already knows about, without going back to the
    /// disk for the other 13999 files. Deleting one file out of 14000 used to
    /// cost a full rescan - the delete itself took 0 ms and the refresh after
    /// it took 189 ms, all of it re-reading rows we already had.
    ///
    /// Paths outside this folder are ignored, so both panels can be handed the
    /// same list and each takes only what concerns it.
    void applyKnownRemovals(const QStringList &paths);
    /// Stats exactly these paths and inserts, updates or drops their rows.
    void applyKnownChanges(const QStringList &paths);

    /**
     * @brief Hold back file-watcher reloads (not explicit ones).
     * A reload resets the model, which destroys and rebuilds every delegate. If
     * that happens while a row is being renamed, the editor is torn down under
     * the user's fingers. Suspend for the duration of the rename; any change
     * seen meanwhile is applied as soon as it resumes.
     */
    Q_INVOKABLE void setReloadSuspended(bool suspended);
    bool reloadSuspended() const { return m_reloadSuspended; }
    Q_INVOKABLE void toggleSort(int column);
    Q_INVOKABLE void toggleSelection(int index);
    /// Counts everything inside this row's folder, on a worker thread,
    /// and puts the total in its Size column. Does nothing for a file or
    /// for "..", so the caller does not have to check first. Asking for a
    /// folder already being counted is ignored rather than started twice.
    Q_INVOKABLE void calculateFolderSize(int index);
    /// Remembers paths as newly written, for every panel. Called when a
    /// copy, move or new folder finishes: those files keep the date they
    /// had at the source, so the clock alone would not notice them.
    static void markRecent(const QStringList &paths);
    Q_INVOKABLE void setRowSelected(int index, bool selected);
    Q_INVOKABLE void selectOnly(int index);
    Q_INVOKABLE void selectRange(int fromIndex, int toIndex, bool clearOthers = true);
    Q_INVOKABLE void beginRightDragSelection(int anchorIndex, bool clearOthers = false);
    Q_INVOKABLE void updateRightDragSelection(int currentIndex);
    Q_INVOKABLE void endRightDragSelection();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deselectAll();
    /// Unmark exactly these paths, leaving every other mark alone. Used after a
    /// successful operation so its source files stop being marked, the way
    /// Total Commander behaves.
    Q_INVOKABLE void deselectPaths(const QStringList &paths);
    Q_INVOKABLE void invertSelection();
    Q_INVOKABLE QStringList getSelectedPaths() const;
    Q_INVOKABLE QString getSelectedSummary() const;
    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE int findItemIndex(const QString &fileName) const;
    /// Names of every row except "..", used to spot items created behind our back.
    QStringList fileNames() const;

signals:
    void currentPathChanged(const QString &path);
    void countChanged();
    void selectionChanged();
    void sortChanged();
    void showHiddenChanged();
    void isLoadingChanged();
    void filterPatternChanged();
    void directoryLoadError(const QString &error);
    void beforeDirectoryReset(bool isNewPath);
    void directoryReset(bool isNewPath);

private:
    void onDirectoryChanged(const QString &path);
    void loadDirectory();
    void sortItems();
    /// The cheap half: applies the filter and the hidden-files switch to what
    /// was already read. This is what runs on every keystroke.
    void rebuildVisibleItems(bool isNewPath, bool announceBefore = true, bool announceAfter = true);
    void setLoading(bool loading);

    /// Keeps the listing for a folder we have already read, so stepping back
    /// into it shows rows immediately instead of an empty panel. The rescan
    /// still runs; the cache only decides what is on screen while it does.
    void storeInCache(const QString &path, const QList<FileItem> &items);
    void touchCache(const QString &path);
    /// True when two listings describe the same folder contents. Selection is
    /// deliberately not compared: it belongs to the user, not to the disk.
    static bool listingsMatch(const QList<FileItem> &a, const QList<FileItem> &b);
    void sortInternal();
    void updateSelectionStats();
    /// Reads a folder into a plain list. Runs on a worker thread, so it takes
    /// everything it needs as arguments and touches no member state; it is a
    /// member only so it can reach the static formatters below.
    static QList<FileItem> scanDirectory(const QString &path, const QSet<QString> &selectedPaths);
    /// Builds one row from one file. Shared by the full scan and the targeted
    /// updates so a row can never be described two different ways.
    static FileItem makeItem(const QFileInfo &info, bool selected);
    /// The sort order, as one predicate. sortInternal uses it to sort; the
    /// targeted insert uses it to find where a new row belongs.
    bool itemLessThan(const FileItem &lhs, const FileItem &rhs) const;
    /// Whether a row survives the current filter and hidden-files switch.
    bool passesView(const FileItem &item, const QList<QRegularExpression> &patterns) const;
    bool belongsToCurrentFolder(const QString &path) const;
    void insertVisibleSorted(const FileItem &item);
    void removeVisibleAt(int row);
    void finishDelta();
    static QString detectFileType(const QFileInfo &info);
    static QString formatSize(qint64 bytes, bool isDir);
    /// Runs on a worker thread: adds up every file below path. Static and
    /// taking only a path, so it touches nothing the GUI thread owns.
    static qint64 directorySize(const QString &path);
    static bool isRecent(const FileItem &item);
    /// Drops marks that have aged out, so the set cannot grow all day.
    static void pruneRecent();
    void onRecentFadeTick();
    void applyFolderSize(const QString &path, qint64 bytes);
    static QString formatPermissions(const QFileInfo &info);

    QString m_currentPath;
    /// Everything the folder holds, read once. m_items is the subset of this
    /// that is currently on screen. Splitting the two is what lets the filter
    /// work without touching the disk.
    QList<FileItem> m_allItems;
    /// The read runs here. Pointing the watcher at a newer future drops the
    /// older one's notification, so a superseded read finishes quietly and its
    /// result is never applied.
    QFutureWatcher<QList<FileItem>> *m_loadWatcher = nullptr;
    bool m_pendingIsNewPath = false;
    bool m_isLoading = false;
    /// Whether the rows currently on screen came from the cache. If they did,
    /// the rescan must not reset the model unless it found a difference.
    bool m_servedFromCache = false;

    /// Folders being counted right now, so pressing Space twice on one
    /// does not start a second walk of the same tree.
    QSet<QString> m_sizeJobs;

    /// Green has to stop on its own, without anything happening in the folder.
    /// This asks the view to re-read that one role now and then.
    QTimer m_recentFadeTimer;
    bool m_hadRecentRows = false;

    QHash<QString, QList<FileItem>> m_listingCache;
    QStringList m_cacheOrder;               // least recently used first
    /// Generous on purpose. An entry costs a hash slot and a string; what
    /// actually bounds the memory is the row budget below, so there is little
    /// point being stingy with the count. A long browsing session keeps its
    /// history rather than losing folders it will come back to.
    /// How long a row stays green.
    ///
    /// A window of time rather than "since the application started": left open
    /// all day, that would keep colouring things that stopped being news hours
    /// ago, and the list of marked paths would grow the whole time. Fifteen
    /// minutes is long enough to still find what a copy produced after looking
    /// somewhere else, and short enough that green keeps meaning "just now".
    static constexpr int kRecentMinutes = 15;

    static constexpr int kCacheFolders = 100;

    /// Roughly 100 MB of cached rows.
    ///
    /// Measured rather than guessed: 100000 rows with names around 45
    /// characters cost about 173 MB resident, so a little under 2 KB each -
    /// mostly the eight separate string allocations per row, not the 232-byte
    /// struct. Short names cost less, so the real figure lands under the
    /// ceiling more often than over it.
    ///
    /// Turned into a row count on purpose. This is a ceiling, not an accounting
    /// system; measuring each listing exactly would mean walking every row on
    /// every store, which is precisely the O(n) work the deltas exist to avoid.
    static constexpr qint64 kBytesPerRow = 2048;
    static constexpr int kCacheRowBudget =
        static_cast<int>((100LL * 1024 * 1024) / kBytesPerRow);   // ~51200 rows
    /// Past this many changed rows, rebuilding the visible list in one go beats
    /// that many individual insertions - each of which shifts the rest of the
    /// list. Still no disk access either way.
    static constexpr int kSurgicalLimit = 32;
    QList<FileItem> m_items;
    QFileSystemWatcher m_watcher;
    // A directory being written to fires directoryChanged once per file. Each
    // reload is a full re-stat of every entry on the GUI thread, so coalesce
    // a burst of changes into a single reload.
    QTimer m_reloadTimer;
    bool m_reloadSuspended = false;
    bool m_reloadPending = false;

    int m_sortColumn = SortByName;
    bool m_sortAscending = true;
    bool m_showHidden = false;
    bool m_isNewPathNavigation = false;
    QString m_filterPattern;

    int m_selectedCount = 0;
    qint64 m_selectedSizeBytes = 0;

    int m_rightDragAnchor = -1;
    bool m_rightDragClearOthers = false;
    bool m_rightDragSelect = true;
    QSet<int> m_rightDragInitialSelection;
};
