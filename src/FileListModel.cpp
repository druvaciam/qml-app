#include "FileListModel.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtConcurrent>
#include <algorithm>

FileListModel::FileListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &FileListModel::onDirectoryChanged);

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(200);
    connect(&m_reloadTimer, &QTimer::timeout, this, &FileListModel::refresh);
}

int FileListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_items.size());
}

QVariant FileListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_items.size())) {
        return QVariant();
    }

    const FileItem &item = m_items[index.row()];

    switch (role) {
    case NameRole:
        return item.name;
    case FullPathRole:
        return item.fullPath;
    case IsDirRole:
        return item.isDir;
    case IsParentRole:
        return item.isParent;
    case IsHiddenRole:
        return item.isHidden;
    case IsExecutableRole:
        return item.isExecutable;
    case SizeRole:
        return item.size;
    case FormattedSizeRole:
        return item.formattedSize;
    case ExtensionRole:
        return item.extension;
    case ModifiedRole:
        return item.lastModified;
    case FormattedModifiedRole:
        return item.formattedModified;
    case FileTypeRole:
        return item.fileType;
    case PermissionsRole:
        return item.permissions;
    case IsSelectedRole:
        return item.isSelected;
    default:
        return QVariant();
    }
}

bool FileListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_items.size())) {
        return false;
    }

    if (role == IsSelectedRole) {
        bool selected = value.toBool();
        if (m_items[index.row()].isParent) {
            return false; // Can't select ".." item
        }
        if (m_items[index.row()].isSelected != selected) {
            m_items[index.row()].isSelected = selected;
            emit dataChanged(index, index, {IsSelectedRole});
            updateSelectionStats();
            return true;
        }
    }
    return false;
}

QHash<int, QByteArray> FileListModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        {NameRole, "fileName"},
        {FullPathRole, "filePath"},
        {IsDirRole, "isDir"},
        {IsParentRole, "isParent"},
        {IsHiddenRole, "isHidden"},
        {IsExecutableRole, "isExecutable"},
        {SizeRole, "fileSize"},
        {FormattedSizeRole, "formattedSize"},
        {ExtensionRole, "extension"},
        {ModifiedRole, "lastModified"},
        {FormattedModifiedRole, "formattedModified"},
        {FileTypeRole, "fileType"},
        {PermissionsRole, "permissions"},
        {IsSelectedRole, "isSelected"}
    };
    return roles;
}

void FileListModel::setCurrentPath(const QString &path)
{
    QString cleanPath = QDir::cleanPath(path);
    if (cleanPath.isEmpty()) {
        cleanPath = QDir::homePath();
    }

    QDir dir(cleanPath);
    if (!dir.exists()) {
        emit directoryLoadError(QString("Directory '%1' does not exist.").arg(cleanPath));
        return;
    }

    if (m_currentPath != cleanPath) {
        m_currentPath = cleanPath;

        // Update watcher
        const auto directories = m_watcher.directories();
        if (!directories.isEmpty()) {
            m_watcher.removePaths(directories);
        }
        m_watcher.addPath(m_currentPath);

        m_isNewPathNavigation = true;
        loadDirectory();
        m_isNewPathNavigation = false;
        emit currentPathChanged(m_currentPath);
    }
}

void FileListModel::setSortColumn(int col)
{
    if (m_sortColumn != col) {
        m_sortColumn = col;
        sortItems();
        emit sortChanged();
    }
}

void FileListModel::setSortAscending(bool ascending)
{
    if (m_sortAscending != ascending) {
        m_sortAscending = ascending;
        sortItems();
        emit sortChanged();
    }
}

void FileListModel::setShowHidden(bool show)
{
    if (m_showHidden != show) {
        m_showHidden = show;
        // Hidden entries are always read; whether they are shown is decided
        // here, in memory. No reason to walk the folder again for a checkbox.
        rebuildVisibleItems(false);
        emit showHiddenChanged();
    }
}

void FileListModel::toggleShowHidden()
{
    setShowHidden(!m_showHidden);
}

void FileListModel::setFilterPattern(const QString &pattern)
{
    if (m_filterPattern != pattern) {
        m_filterPattern = pattern;
        // The folder has not changed - only how much of it we are showing.
        // This used to re-read the directory, so typing eight characters into
        // the filter did eight full directory reads. At 14000 files that was
        // roughly a quarter of a second of disk work per keystroke.
        rebuildVisibleItems(false);
        emit filterPatternChanged();
    }
}

bool FileListModel::isCopyScratchFile(const QString &fileName)
{
    return fileName.endsWith(QStringLiteral(".qmlcommander-part"));
}

void FileListModel::clearFilterNoReload()
{
    if (!m_filterPattern.isEmpty()) {
        m_filterPattern.clear();
        emit filterPatternChanged();
    }
}

void FileListModel::refreshItem(const QString &filePath)
{
    if (filePath.isEmpty() || m_items.isEmpty()) {
        return;
    }

    auto key = [](const QString &p) {
#ifdef Q_OS_WIN
        return QDir::cleanPath(p).toLower();
#else
        return QDir::cleanPath(p);
#endif
    };
    const QString wanted = key(filePath);

    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (key(m_items[i].fullPath) != wanted) {
            continue;
        }

        // Keep the unfiltered list in step, otherwise the next filter change
        // rebuilds the row from stale data and the update disappears.
        for (int j = 0; j < static_cast<int>(m_allItems.size()); ++j) {
            if (key(m_allItems[j].fullPath) == wanted) {
                m_allItems[j].lastModified = QFileInfo(m_allItems[j].fullPath).lastModified();
                m_allItems[j].size = QFileInfo(m_allItems[j].fullPath).size();
                break;
            }
        }

        const QFileInfo info(m_items[i].fullPath);
        if (!info.exists()) {
            // Gone rather than changed; leave it to the watcher's reload.
            return;
        }

        FileItem &item = m_items[i];
        item.size = item.isDir ? 0 : info.size();
        item.formattedSize = formatSize(item.size, item.isDir);
        item.lastModified = info.lastModified();
        item.formattedModified = item.lastModified.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        item.permissions = formatPermissions(info);

        const QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, {SizeRole, FormattedSizeRole, ModifiedRole,
                                    FormattedModifiedRole, PermissionsRole});
        // The status bar totals the size of marked items, so they can change too.
        if (item.isSelected) {
            updateSelectionStats();
        }
        return;
    }
}

void FileListModel::refresh()
{
    m_reloadTimer.stop();
    m_reloadPending = false;
    loadDirectory();
}

void FileListModel::toggleSort(int column)
{
    if (m_sortColumn == column) {
        setSortAscending(!m_sortAscending);
    } else {
        m_sortColumn = column;
        m_sortAscending = true;
        sortItems();
        emit sortChanged();
    }
}

void FileListModel::toggleSelection(int index)
{
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        if (!m_items[index].isParent) {
            bool nextState = !m_items[index].isSelected;
            m_items[index].isSelected = nextState;
            QModelIndex idx = this->index(index, 0);
            emit dataChanged(idx, idx, {IsSelectedRole});
            updateSelectionStats();
        }
    }
}

void FileListModel::setRowSelected(int index, bool selected)
{
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        if (!m_items[index].isParent && m_items[index].isSelected != selected) {
            m_items[index].isSelected = selected;
            QModelIndex idx = this->index(index, 0);
            emit dataChanged(idx, idx, {IsSelectedRole});
            updateSelectionStats();
        }
    }
}

void FileListModel::selectOnly(int index)
{
    bool changed = false;
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        bool shouldSelect = (i == index && !m_items[i].isParent);
        if (m_items[i].isSelected != shouldSelect) {
            m_items[i].isSelected = shouldSelect;
            changed = true;
        }
    }
    if (changed && !m_items.isEmpty()) {
        emit dataChanged(this->index(0, 0), this->index(static_cast<int>(m_items.size()) - 1, 0), {IsSelectedRole});
        updateSelectionStats();
    }
}

void FileListModel::selectRange(int fromIndex, int toIndex, bool clearOthers)
{
    if (m_items.isEmpty()) return;
    int start = std::clamp(std::min(fromIndex, toIndex), 0, static_cast<int>(m_items.size()) - 1);
    int end = std::clamp(std::max(fromIndex, toIndex), 0, static_cast<int>(m_items.size()) - 1);

    bool changed = false;
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (m_items[i].isParent) {
            if (m_items[i].isSelected) {
                m_items[i].isSelected = false;
                changed = true;
            }
            continue;
        }
        bool inRange = (i >= start && i <= end);
        bool shouldSelect = clearOthers ? inRange : (m_items[i].isSelected || inRange);
        if (m_items[i].isSelected != shouldSelect) {
            m_items[i].isSelected = shouldSelect;
            changed = true;
        }
    }
    if (changed) {
        emit dataChanged(index(0, 0), index(static_cast<int>(m_items.size()) - 1, 0), {IsSelectedRole});
        updateSelectionStats();
    }
}

void FileListModel::beginRightDragSelection(int anchorIndex, bool clearOthers)
{
    m_rightDragAnchor = anchorIndex;
    m_rightDragClearOthers = clearOthers;
    m_rightDragInitialSelection.clear();

    if (anchorIndex >= 0 && anchorIndex < static_cast<int>(m_items.size())) {
        // If anchor was already selected, mirror mode is to DESELECT
        m_rightDragSelect = !m_items[anchorIndex].isSelected;
    } else {
        m_rightDragSelect = true;
    }

    if (!clearOthers) {
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            if (m_items[i].isSelected) {
                m_rightDragInitialSelection.insert(i);
            }
        }
    }
    updateRightDragSelection(anchorIndex);
}

void FileListModel::updateRightDragSelection(int currentIndex)
{
    if (m_rightDragAnchor < 0 || m_items.isEmpty()) return;
    int start = std::clamp(std::min(m_rightDragAnchor, currentIndex), 0, static_cast<int>(m_items.size()) - 1);
    int end = std::clamp(std::max(m_rightDragAnchor, currentIndex), 0, static_cast<int>(m_items.size()) - 1);

    bool changed = false;
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (m_items[i].isParent) {
            if (m_items[i].isSelected) {
                m_items[i].isSelected = false;
                changed = true;
            }
            continue;
        }
        bool inRange = (i >= start && i <= end);
        bool shouldSelect = false;
        if (m_rightDragSelect) {
            // Select mode: inRange items are selected, others keep initial state
            shouldSelect = inRange || (!m_rightDragClearOthers && m_rightDragInitialSelection.contains(i));
        } else {
            // Deselect mode: inRange items are deselected, others keep initial state
            shouldSelect = !inRange && m_rightDragInitialSelection.contains(i);
        }
        if (m_items[i].isSelected != shouldSelect) {
            m_items[i].isSelected = shouldSelect;
            changed = true;
        }
    }
    if (changed) {
        emit dataChanged(index(0, 0), index(static_cast<int>(m_items.size()) - 1, 0), {IsSelectedRole});
        updateSelectionStats();
    }
}

void FileListModel::endRightDragSelection()
{
    m_rightDragAnchor = -1;
    m_rightDragInitialSelection.clear();
}

void FileListModel::selectAll()
{
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (!m_items[i].isParent) {
            m_items[i].isSelected = true;
        }
    }
    if (!m_items.isEmpty()) {
        emit dataChanged(index(0, 0), index(static_cast<int>(m_items.size()) - 1, 0), {IsSelectedRole});
    }
    updateSelectionStats();
}

void FileListModel::deselectAll()
{
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        m_items[i].isSelected = false;
    }
    if (!m_items.isEmpty()) {
        emit dataChanged(index(0, 0), index(static_cast<int>(m_items.size()) - 1, 0), {IsSelectedRole});
    }
    updateSelectionStats();
}

void FileListModel::deselectPaths(const QStringList &paths)
{
    if (paths.isEmpty() || m_items.isEmpty()) {
        return;
    }

    // Paths can arrive from a drop or the clipboard with different separators or
    // letter case, so compare them the way the filesystem would.
    auto key = [](const QString &p) {
#ifdef Q_OS_WIN
        return QDir::cleanPath(p).toLower();
#else
        return QDir::cleanPath(p);
#endif
    };

    QSet<QString> drop;
    drop.reserve(paths.size());
    for (const QString &p : paths) {
        drop.insert(key(p));
    }

    bool changed = false;
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (m_items[i].isSelected && drop.contains(key(m_items[i].fullPath))) {
            m_items[i].isSelected = false;
            changed = true;
        }
    }

    if (changed) {
        emit dataChanged(index(0, 0), index(static_cast<int>(m_items.size()) - 1, 0), {IsSelectedRole});
        updateSelectionStats();
    }
}

void FileListModel::invertSelection()
{
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (!m_items[i].isParent) {
            m_items[i].isSelected = !m_items[i].isSelected;
        }
    }
    if (!m_items.isEmpty()) {
        emit dataChanged(index(0, 0), index(static_cast<int>(m_items.size()) - 1, 0), {IsSelectedRole});
    }
    updateSelectionStats();
}

QStringList FileListModel::getSelectedPaths() const
{
    QStringList paths;
    for (const auto &item : m_items) {
        if (item.isSelected && !item.isParent) {
            paths.append(item.fullPath);
        }
    }
    return paths;
}

QString FileListModel::getSelectedSummary() const
{
    if (m_selectedCount == 0) {
        return QStringLiteral("0 items selected");
    }
    return QString("%1 item(s) selected (%2)").arg(m_selectedCount).arg(selectedSizeFormatted());
}

QVariantMap FileListModel::get(int index) const
{
    QVariantMap map;
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        const auto &it = m_items[index];
        map["fileName"] = it.name;
        map["filePath"] = it.fullPath;
        map["isDir"] = it.isDir;
        map["isParent"] = it.isParent;
        map["isHidden"] = it.isHidden;
        map["isExecutable"] = it.isExecutable;
        map["fileSize"] = it.size;
        map["formattedSize"] = it.formattedSize;
        map["extension"] = it.extension;
        map["lastModified"] = it.lastModified;
        map["formattedModified"] = it.formattedModified;
        map["fileType"] = it.fileType;
        map["permissions"] = it.permissions;
        map["isSelected"] = it.isSelected;
    }
    return map;
}

QStringList FileListModel::fileNames() const
{
    QStringList names;
    names.reserve(m_items.size());
    for (const auto &item : m_items) {
        if (!item.isParent) {
            names.append(item.name);
        }
    }
    return names;
}

int FileListModel::findItemIndex(const QString &fileName) const
{
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (m_items[i].name.compare(fileName, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

QString FileListModel::selectedSizeFormatted() const
{
    return formatSize(m_selectedSizeBytes, false);
}

void FileListModel::onDirectoryChanged(const QString &path)
{
    Q_UNUSED(path)
    if (m_reloadSuspended) {
        m_reloadPending = true;
        return;
    }
    // Restart on every notification so a burst collapses into one reload once
    // the directory has been quiet for the timer interval.
    m_reloadTimer.start();
}

void FileListModel::setReloadSuspended(bool suspended)
{
    if (m_reloadSuspended == suspended) {
        return;
    }
    m_reloadSuspended = suspended;

    if (suspended) {
        // A reload may already be queued from the change that prompted this.
        if (m_reloadTimer.isActive()) {
            m_reloadTimer.stop();
            m_reloadPending = true;
        }
        return;
    }

    if (m_reloadPending) {
        m_reloadPending = false;
        m_reloadTimer.start();
    }
}

void FileListModel::setLoading(bool loading)
{
    if (m_isLoading != loading) {
        m_isLoading = loading;
        emit isLoadingChanged();
    }
}

void FileListModel::loadDirectory()
{
    const bool isNewPath = m_isNewPathNavigation;

    QSet<QString> keepSelected;
    if (!isNewPath) {
        for (const auto &it : m_allItems) {
            if (it.isSelected) {
                keepSelected.insert(it.fullPath);
            }
        }
    }

    // Announced once, here, before the rows go away - the view saves its scroll
    // position and cursor on this. The matching directoryReset comes when the
    // results land, so the view still sees exactly one pair per load.
    emit beforeDirectoryReset(isNewPath);

    m_pendingIsNewPath = isNewPath;

    // Only a real folder change empties the list. A refresh - after a paste, or
    // when the watcher notices something - is a reload of the folder already on
    // screen, and those rows stay valid until the new ones arrive. Blanking
    // them made a one-file paste into a large folder look like the whole
    // listing had vanished and come back.
    if (isNewPath) {
        m_allItems.clear();
        rebuildVisibleItems(isNewPath, false, false);
    }

    setLoading(true);

    if (!m_loadWatcher) {
        m_loadWatcher = new QFutureWatcher<QList<FileItem>>(this);
        connect(m_loadWatcher, &QFutureWatcherBase::finished, this, [this]() {
            if (!m_loadWatcher->future().isFinished()) {
                return;
            }
            m_allItems = m_loadWatcher->result();
            rebuildVisibleItems(m_pendingIsNewPath, false, true);
            setLoading(false);
        });
    }

    // Pointing the watcher at a new future drops the old one's notification,
    // which is exactly what should happen when navigation outruns the disk.
    m_loadWatcher->setFuture(QtConcurrent::run(&FileListModel::scanDirectory, m_currentPath, keepSelected));
}

/// Splits the filter text into the wildcard patterns to match names against.
/// A bare word becomes *word*, so partial names work; a token containing * or ?
/// is used as written. Separators are comma, semicolon, or spaces when the text
/// already looks like a wildcard list.
static QList<QRegularExpression> compileFilter(const QString &filterText)
{
    QList<QRegularExpression> compiled;
    const QString raw = filterText.trimmed();
    if (raw.isEmpty()) {
        return compiled;
    }

    QStringList tokens;
    if (raw.contains(QLatin1Char(';')) || raw.contains(QLatin1Char(','))) {
        tokens = raw.split(QRegularExpression(QStringLiteral("[,;]+")), Qt::SkipEmptyParts);
    } else if (raw.contains(QLatin1Char(' ')) && (raw.contains(QLatin1Char('*')) || raw.contains(QLatin1Char('?')))) {
        tokens = raw.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    } else {
        tokens << raw;
    }

    for (const QString &tok : tokens) {
        const QString trimmedTok = tok.trimmed();
        if (trimmedTok.isEmpty()) {
            continue;
        }
        const QString wildcard = (!trimmedTok.contains(QLatin1Char('*')) && !trimmedTok.contains(QLatin1Char('?')))
                                     ? QStringLiteral("*%1*").arg(trimmedTok)
                                     : trimmedTok;
        // Matched the way QDir would have matched it, so moving the filter off
        // the disk does not quietly change which names it accepts: fold case on
        // Windows, respect it elsewhere.
        QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
#ifdef Q_OS_WIN
        opts |= QRegularExpression::CaseInsensitiveOption;
#endif
        compiled << QRegularExpression(QRegularExpression::wildcardToRegularExpression(wildcard), opts);
    }
    return compiled;
}

/// Runs on a worker thread, so it touches nothing but its arguments. The three
/// formatters it calls are static and stateless. Reading a folder of 14000
/// files costs upwards of a quarter of a second, and doing that on the GUI
/// thread froze the window outright for as long as it took.
QList<FileItem> FileListModel::scanDirectory(const QString &path, const QSet<QString> &selectedPaths)
{
    QList<FileItem> out;

    if (path.isEmpty()) {
        return out;
    }
    QDir dir(path);
    if (!dir.exists()) {
        return out;
    }

    // Hidden and system entries are always read. Whether they are shown is
    // decided in rebuildVisibleItems, so toggling the switch costs nothing.
    const QFileInfoList entries =
        dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDir::NoSort);

    out.reserve(entries.size());
    for (const QFileInfo &info : entries) {
        // A copy in progress writes one of these per file. They are the app's,
        // not the user's, and are never worth showing.
        if (FileListModel::isCopyScratchFile(info.fileName())) {
            continue;
        }

        FileItem item;
        item.name = info.fileName();
        item.fullPath = info.absoluteFilePath();
        item.isDir = info.isDir();
        item.isParent = false;
        item.isHidden = info.isHidden() || info.fileName().startsWith(QLatin1Char('.'));
        item.isExecutable = info.isExecutable() && !item.isDir;
        item.size = item.isDir ? 0 : info.size();
        item.formattedSize = FileListModel::formatSize(item.size, item.isDir);
        item.extension = item.isDir ? QString() : info.suffix().toLower();
        // Folders are not split at a dot: a folder called "my.stuff" is named
        // that, it does not have an extension.
        item.baseName = item.isDir ? item.name : info.completeBaseName();
        item.lastModified = info.lastModified();
        item.formattedModified = item.lastModified.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        item.fileType = FileListModel::detectFileType(info);
        item.permissions = FileListModel::formatPermissions(info);
        item.isSelected = selectedPaths.contains(item.fullPath);

        out.append(item);
    }
    return out;
}

void FileListModel::rebuildVisibleItems(bool isNewPath, bool announceBefore, bool announceAfter)
{
    if (announceBefore) {
        emit beforeDirectoryReset(isNewPath);
    }

    // Carry the selection of the rows currently on screen back into the full
    // list, so it survives this rebuild. Rows hidden by the filter keep what
    // they had: filtering something out of view is not deselecting it.
    if (!m_items.isEmpty() && !m_allItems.isEmpty()) {
        QHash<QString, bool> onScreen;
        onScreen.reserve(m_items.size());
        for (const auto &it : m_items) {
            if (!it.isParent) {
                onScreen.insert(it.fullPath, it.isSelected);
            }
        }
        for (auto &it : m_allItems) {
            const auto found = onScreen.constFind(it.fullPath);
            if (found != onScreen.constEnd()) {
                it.isSelected = found.value();
            }
        }
    }

    const QList<QRegularExpression> patterns = compileFilter(m_filterPattern);

    beginResetModel();
    m_items.clear();
    m_items.reserve(m_allItems.size() + 1);

    // The ".." row is built before any filtering and is never subject to it,
    // so no filter can strand you in a folder you cannot leave.
    if (!m_currentPath.isEmpty()) {
        QDir dir(m_currentPath);
        if (dir.exists() && !dir.isRoot()) {
            FileItem parentItem;
            parentItem.name = QStringLiteral("..");
            parentItem.fullPath = QDir::cleanPath(dir.absoluteFilePath(QStringLiteral("..")));
            parentItem.isDir = true;
            parentItem.isParent = true;
            parentItem.baseName = parentItem.name;
            parentItem.formattedSize = QStringLiteral("<DIR>");
            parentItem.fileType = QStringLiteral("parent");
            parentItem.formattedModified = QStringLiteral("");
            m_items.append(parentItem);
        }
    }

    for (const FileItem &item : m_allItems) {
        if (!m_showHidden && item.isHidden) {
            continue;
        }
        if (!patterns.isEmpty()) {
            bool matched = false;
            for (const QRegularExpression &re : patterns) {
                if (re.match(item.name).hasMatch()) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                continue;
            }
        }
        m_items.append(item);
    }

    sortInternal();
    endResetModel();
    emit countChanged();
    updateSelectionStats();
    if (announceAfter) {
        emit directoryReset(isNewPath);
    }
}

/// Compares two names the way a person reads them: a run of digits counts as
/// one number, everything else as text. Plain string comparison put "(10)"
/// before "(2)", because it compares "1" against "2" and stops there - obvious
/// nonsense in a folder of numbered copies. Explorer and Total Commander both
/// sort this way.
///
/// Returns <0, 0 or >0. This is a total order, which std::stable_sort requires:
/// every branch either returns or advances both cursors, and the one case that
/// cannot be decided on the spot - "01" against "1", equal as numbers - is
/// remembered and only applied if nothing else separates the two names.
static int compareNatural(const QString &a, const QString &b)
{
    const int na = a.size();
    const int nb = b.size();
    int i = 0;
    int j = 0;
    int zeroTie = 0;   // decides "01" vs "1", and only as a last resort

    while (i < na && j < nb) {
        const QChar ca = a.at(i);
        const QChar cb = b.at(j);

        if (ca.isDigit() && cb.isDigit()) {
            int si = i;
            int sj = j;
            while (si < na && a.at(si) == QLatin1Char('0')) ++si;
            while (sj < nb && b.at(sj) == QLatin1Char('0')) ++sj;

            int ei = si;
            int ej = sj;
            while (ei < na && a.at(ei).isDigit()) ++ei;
            while (ej < nb && b.at(ej).isDigit()) ++ej;

            const int lenA = ei - si;
            const int lenB = ej - sj;
            if (lenA != lenB) {
                return lenA < lenB ? -1 : 1;     // more digits means a bigger number
            }
            for (int k = 0; k < lenA; ++k) {
                if (a.at(si + k) != b.at(sj + k)) {
                    return a.at(si + k) < b.at(sj + k) ? -1 : 1;
                }
            }
            if (zeroTie == 0 && (si - i) != (sj - j)) {
                zeroTie = (si - i) < (sj - j) ? -1 : 1;
            }
            i = ei;
            j = ej;
            continue;
        }

        const QChar fa = ca.toCaseFolded();
        const QChar fb = cb.toCaseFolded();
        if (fa != fb) {
            return fa < fb ? -1 : 1;
        }
        ++i;
        ++j;
    }

    if (i < na) {
        return 1;
    }
    if (j < nb) {
        return -1;
    }
    return zeroTie;
}

/// Compares two names the way Total Commander's Name column does: the name
/// without its extension first, the extension only to break a tie.
///
/// Comparing whole file names put every "payload - copy (N).txt" *above*
/// "payload.txt", because the comparison reaches the space of " - copy" and the
/// dot of ".txt" at the same position, and a space sorts before a dot. The
/// original file ended up below thousands of its own copies.
static int compareByName(const FileItem &a, const FileItem &b)
{
    const int byBase = compareNatural(a.baseName, b.baseName);
    if (byBase != 0) {
        return byBase;
    }
    const int byExt = compareNatural(a.extension, b.extension);
    if (byExt != 0) {
        return byExt;
    }
    return a.name.compare(b.name, Qt::CaseInsensitive);
}

void FileListModel::sortInternal()
{
    if (m_items.isEmpty()) {
        return;
    }

    // Keep parent item ".." at top index 0 if present
    int startIndex = 0;
    if (!m_items.isEmpty() && m_items.first().isParent) {
        startIndex = 1;
    }

    if (startIndex >= static_cast<int>(m_items.size())) {
        return;
    }

    auto beginIter = m_items.begin() + startIndex;
    auto endIter = m_items.end();

    std::stable_sort(beginIter, endIter, [this](const FileItem &lhs, const FileItem &rhs) {
        // Directories always first, in both sort directions
        if (lhs.isDir != rhs.isDir) {
            return lhs.isDir > rhs.isDir;
        }

        // Swap the operands for a descending sort rather than negating the result:
        // negating would return true for two equal items in both directions, which
        // is not a strict weak ordering and is undefined behaviour for stable_sort.
        const FileItem &a = m_sortAscending ? lhs : rhs;
        const FileItem &b = m_sortAscending ? rhs : lhs;

        switch (m_sortColumn) {
        case SortByExt:
            if (a.extension != b.extension) {
                return compareNatural(a.extension, b.extension) < 0;
            }
            return compareByName(a, b) < 0;
        case SortBySize:
            if (a.size != b.size) {
                return a.size < b.size;
            }
            return compareByName(a, b) < 0;
        case SortByDate:
            if (a.lastModified != b.lastModified) {
                return a.lastModified < b.lastModified;
            }
            return compareByName(a, b) < 0;
        case SortByName:
        default:
            return compareByName(a, b) < 0;
        }
    });
}

void FileListModel::sortItems()
{
    if (m_items.isEmpty()) return;
    beginResetModel();
    sortInternal();
    endResetModel();
}

void FileListModel::updateSelectionStats()
{
    int count = 0;
    qint64 totalBytes = 0;

    for (const auto &item : m_items) {
        if (item.isSelected && !item.isParent) {
            count++;
            totalBytes += item.size;
        }
    }

    if (m_selectedCount != count || m_selectedSizeBytes != totalBytes) {
        m_selectedCount = count;
        m_selectedSizeBytes = totalBytes;
        emit selectionChanged();
    }
}

QString FileListModel::detectFileType(const QFileInfo &info)
{
    if (info.isDir()) {
        return QStringLiteral("folder");
    }

    const QString ext = info.suffix().toLower();
    
    // Code & Markup
    if (ext == "cpp" || ext == "c" || ext == "h" || ext == "hpp" || ext == "qml" ||
        ext == "js" || ext == "ts" || ext == "html" || ext == "css" || ext == "py" ||
        ext == "json" || ext == "xml" || ext == "yaml" || ext == "yml" || ext == "md" ||
        ext == "cmake" || ext == "txt" || ext == "ini" || ext == "conf" || ext == "sh" ||
        ext == "bat" || ext == "ps1") {
        return QStringLiteral("code");
    }
    // Images
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "bmp" ||
        ext == "svg" || ext == "webp" || ext == "ico" || ext == "tiff") {
        return QStringLiteral("image");
    }
    // Archives
    if (ext == "zip" || ext == "rar" || ext == "7z" || ext == "tar" || ext == "gz" ||
        ext == "bz2" || ext == "xz") {
        return QStringLiteral("archive");
    }
    // Executables
    if (ext == "exe" || ext == "msi" || ext == "dll" || ext == "so" || ext == "bin") {
        return QStringLiteral("executable");
    }
    // Documents
    if (ext == "pdf" || ext == "doc" || ext == "docx" || ext == "xls" || ext == "xlsx" ||
        ext == "ppt" || ext == "pptx" || ext == "csv") {
        return QStringLiteral("document");
    }
    // Media Audio/Video
    if (ext == "mp3" || ext == "wav" || ext == "flac" || ext == "ogg" || ext == "m4a") {
        return QStringLiteral("audio");
    }
    if (ext == "mp4" || ext == "mkv" || ext == "avi" || ext == "mov" || ext == "webm") {
        return QStringLiteral("video");
    }

    return QStringLiteral("file");
}

QString FileListModel::formatSize(qint64 bytes, bool isDir)
{
    if (isDir) {
        return QStringLiteral("<DIR>");
    }

    if (bytes < 0) return QStringLiteral("0 B");

    constexpr double KB = 1024.0;
    constexpr double MB = 1024.0 * KB;
    constexpr double GB = 1024.0 * MB;

    double dBytes = static_cast<double>(bytes);
    if (dBytes >= GB) {
        return QString::asprintf("%.2f GB", dBytes / GB);
    } else if (dBytes >= MB) {
        return QString::asprintf("%.1f MB", dBytes / MB);
    } else if (dBytes >= KB) {
        return QString::asprintf("%.1f KB", dBytes / KB);
    } else {
        return QString::asprintf("%lld B", bytes);
    }
}

QString FileListModel::formatPermissions(const QFileInfo &info)
{
    QString p;
    auto perms = info.permissions();
    p += (perms & QFile::ReadUser) ? 'r' : '-';
    p += (perms & QFile::WriteUser) ? 'w' : '-';
    p += (perms & QFile::ExeUser) ? 'x' : '-';
    return p;
}
