import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

// A FocusScope, not a plain Item. The ListView inside declares `focus: true`;
// without a scope here that claim propagates up into FilePanel's scope and
// competes with the quick-filter field. Every keystroke in the filter resets the
// model, which churns the list, and the competition resolved back to the list -
// so the filter lost focus after each character.
FocusScope {
    id: rootListView
    required property PanelController controller

    signal itemActivated(int index, string path, bool isDir)
    signal requestContextMenu(int index, real globalX, real globalY)
    signal requestDelete(bool permanent)
    signal requestCopy()
    signal requestMove()
    signal requestDropCopy(var paths, string destination, bool isMove)

    readonly property bool containsDrag: listDropArea.containsDrag
    property int editingIndex: -1
    // The row being renamed is tracked by name as well as index. A background
    // reload renumbers rows, and creating a file wakes the watcher, so without
    // this the rename box would disappear a moment after opening.
    property string editingName: ""
    // What the user has typed so far. Held here rather than in the editor so it
    // survives if the row is rebuilt mid-rename by an explicit refresh.
    property string editingText: ""
    property int dragHoverIndex: -1
    property int selectionAnchorIndex: -1

    property string savedTopItemName: ""
    property real savedTopItemOffset: 0
    property string savedCurrentItemName: ""
    property real savedContentY: 0
    property bool isReloadingSamePath: false

    function saveViewState() {
        if (!listView || !rootListView.controller || !rootListView.controller.model) return
        savedContentY = listView.contentY

        let model = rootListView.controller.model
        let topIdx = listView.indexAt(10, listView.contentY + 5)
        if (topIdx >= 0 && topIdx < model.count) {
            let item = model.get(topIdx)
            savedTopItemName = (item && item.fileName) ? item.fileName : ""
            savedTopItemOffset = listView.contentY - (topIdx * Theme.rowHeight)
        } else {
            savedTopItemName = ""
            savedTopItemOffset = 0
        }

        let curIdx = listView.currentIndex
        if (curIdx >= 0 && curIdx < model.count) {
            let curItem = model.get(curIdx)
            savedCurrentItemName = (curItem && curItem.fileName) ? curItem.fileName : ""
        } else {
            savedCurrentItemName = ""
        }
    }

    function restoreViewState() {
        if (!listView || !rootListView.controller || !rootListView.controller.model) return
        let model = rootListView.controller.model
        if (model.count === 0) return

        // 1. Restore current item index by name
        if (savedCurrentItemName !== "") {
            let newCurIdx = model.findItemIndex(savedCurrentItemName)
            if (newCurIdx >= 0) {
                listView.currentIndex = newCurIdx
                rootListView.controller.currentIndex = newCurIdx
            } else if (model.count > 0) {
                let clamped = Math.min(Math.max(0, rootListView.controller.currentIndex), model.count - 1)
                listView.currentIndex = clamped
                rootListView.controller.currentIndex = clamped
            }
        } else if (model.count > 0) {
            let clamped = Math.min(Math.max(0, rootListView.controller.currentIndex), model.count - 1)
            listView.currentIndex = clamped
            rootListView.controller.currentIndex = clamped
        }

        // 2. Restore scroll position anchored to top-visible item
        if (savedTopItemName !== "") {
            let newTopIdx = model.findItemIndex(savedTopItemName)
            if (newTopIdx >= 0) {
                let targetY = Math.max(0, newTopIdx * Theme.rowHeight + savedTopItemOffset)
                let maxY = Math.max(0, listView.contentHeight - listView.height)
                listView.contentY = Math.min(targetY, maxY)
                return
            }
        }

        // Fallback: restore savedContentY clamped to bounds
        let maxY = Math.max(0, listView.contentHeight - listView.height)
        listView.contentY = Math.min(Math.max(0, savedContentY), maxY)
    }

    function updateDragHover(viewX, viewY) {
        let contentPos = mapToItem(listView.contentItem, viewX, viewY)
        let idx = listView.indexAt(contentPos.x, contentPos.y)
        if (idx >= 0 && idx < listView.count) {
            let item = rootListView.controller.model.get(idx)
            if (item && item.isDir) {
                dragHoverIndex = idx
                return
            }
        }
        dragHoverIndex = -1
    }

    function clearDragHover() {
        dragHoverIndex = -1
    }

    function getHoveredFolder() {
        if (dragHoverIndex >= 0 && dragHoverIndex < listView.count) {
            let item = rootListView.controller.model.get(dragHoverIndex)
            if (item && item.isDir) {
                return item.filePath
            }
        }
        return ""
    }

    // One long-press timer and one rename timer for the whole view. Previously
    // each of the three empty-area handlers carried its own copy, and every row
    // delegate built two more, so a 40-row list created 80 Timer objects.
    Timer {
        id: longPressTimer
        interval: 400
        repeat: false
        property int targetIndex: -1
        property real globalX: 0
        property real globalY: 0
        onTriggered: rootListView.controller.showContextMenu(globalX, globalY, targetIndex)
    }

    Timer {
        id: renameClickTimer
        interval: 450
        repeat: false
        property int targetIndex: -1
        onTriggered: {
            if (rootListView.editingIndex !== -1) return
            if (targetIndex < 0 || listView.currentIndex !== targetIndex) return
            if (!rootListView.controller.isActive) return
            rootListView.beginInlineRename(targetIndex)
        }
    }

    /// Arm the context menu for a right press. globalPt comes from mapToGlobal;
    /// index is -1 for the folder background.
    function armLongPress(globalPt, index) {
        longPressTimer.globalX = globalPt.x
        longPressTimer.globalY = globalPt.y
        longPressTimer.targetIndex = index
        longPressTimer.start()
    }

    // Keeps a right-drag selection moving while the pointer is held outside the
    // list. onPositionChanged only fires when the mouse actually moves, so
    // holding it below the last row used to stop the selection dead.
    Timer {
        id: dragScrollTimer
        interval: 50
        repeat: true
        property int direction: 0     // -1 up, +1 down, 0 idle
        property real overshoot: 0    // pixels beyond the edge, sets the speed
        onTriggered: {
            if (direction === 0) { stop(); return }
            // Further out means faster, the way a text selection behaves.
            let rows = Math.max(1, Math.min(10, Math.floor(overshoot / 12)))
            let next = Math.max(0, Math.min(listView.count - 1,
                                            listView.currentIndex + direction * rows))
            if (next === listView.currentIndex) { stop(); return }  // already at the end
            rootListView.applyRightDragTo(next)
        }
    }

    /// Called as the pointer moves during a right-drag. viewY is in listView
    /// coordinates, so it is negative above the list and > height below it.
    function updateRightDragAutoScroll(viewY) {
        if (viewY < 0) {
            dragScrollTimer.direction = -1
            dragScrollTimer.overshoot = -viewY
        } else if (viewY > listView.height) {
            dragScrollTimer.direction = 1
            dragScrollTimer.overshoot = viewY - listView.height
        } else {
            dragScrollTimer.direction = 0
        }

        if (dragScrollTimer.direction === 0) {
            dragScrollTimer.stop()
        } else if (!dragScrollTimer.running) {
            dragScrollTimer.start()
        }
    }

    function stopDragAutoScroll() {
        dragScrollTimer.stop()
        dragScrollTimer.direction = 0
    }

    /// Extend a right-drag selection to targetIdx.
    ///
    /// Deliberately a function on this item rather than inline in the delegate.
    /// Scrolling to the target recycles delegates, so a delegate that called
    /// this can be destroyed by the very first line of it. Once execution is
    /// inside this function the names resolve in the root's scope and survive
    /// that; inline, the next statement hit "rootListView is undefined".
    function applyRightDragTo(targetIdx) {
        if (targetIdx < 0 || targetIdx >= listView.count) return
        rootListView.controller.model.updateRightDragSelection(targetIdx)
        // listView.onCurrentIndexChanged pushes this through to the controller,
        // so setting controller.currentIndex here as well would be redundant.
        listView.currentIndex = targetIdx
        listView.positionViewAtIndex(targetIdx, ListView.Contain)
    }

    /// Move the cursor by delta rows, extending the selection while Shift is held.
    ///
    /// Up and Down have dedicated Keys handlers, and QML gives those the event
    /// before the general Keys.onPressed - so a Shift+Up branch written in
    /// onPressed never ran. Both entry points call this instead.
    function moveCursorBy(delta, modifiers) {
        if (listView.count === 0) return
        let target = Math.max(0, Math.min(listView.count - 1, listView.currentIndex + delta))

        if (modifiers & Qt.ShiftModifier) {
            if (rootListView.selectionAnchorIndex < 0) {
                rootListView.selectionAnchorIndex = listView.currentIndex
            }
            listView.currentIndex = target
            // Ctrl+Shift adds to what is already marked instead of replacing it.
            rootListView.controller.model.selectRange(rootListView.selectionAnchorIndex, target,
                                                      !(modifiers & Qt.ControlModifier))
        } else {
            // Moving without Shift re-anchors, so the next Shift range starts here.
            rootListView.selectionAnchorIndex = target
            listView.currentIndex = target
        }
        listView.positionViewAtIndex(target, ListView.Contain)
    }

    /// How many whole rows fit in the viewport. Page Up/Down move by one less
    /// than this, so the row you were looking at stays on screen as an anchor.
    function rowsPerPage() {
        return Math.max(1, Math.floor(listView.height / Theme.rowHeight))
    }

    function longPressPending() { return longPressTimer.running }
    function cancelLongPress() { longPressTimer.stop() }

    /// A right press released before the timer fired: show the menu immediately.
    function releaseLongPress(globalPt) {
        if (!longPressTimer.running) return false
        longPressTimer.stop()
        rootListView.controller.showContextMenu(globalPt.x, globalPt.y, longPressTimer.targetIndex)
        return true
    }

    function startInlineRename() {
        beginInlineRename(listView.currentIndex)
    }

    function beginInlineRename(idx) {
        if (idx < 0 || idx >= listView.count) return
        let map = rootListView.controller.model.get(idx)
        if (!map || !map.filePath || map.isParent) return
        renameClickTimer.stop()
        // Stop watcher reloads from pulling the editor apart while it is open.
        rootListView.controller.model.setReloadSuspended(true)
        rootListView.editingName = String(map.fileName || "")
        rootListView.editingText = ""
        rootListView.editingIndex = idx
        listView.currentIndex = idx
        rootListView.controller.currentIndex = idx
        // A reload may already have queued a restoreViewState(); make it restore
        // to this row rather than the one that was current a moment ago.
        rootListView.savedCurrentItemName = rootListView.editingName
        listView.positionViewAtIndex(idx, ListView.Contain)
    }

    function endInlineRename() {
        // Also drop a rename that is merely pending: a click arms a 450 ms
        // timer, and anything opened in the meantime would find the editor
        // appearing on top of it a moment later.
        renameClickTimer.stop()
        rootListView.editingIndex = -1
        rootListView.editingName = ""
        rootListView.editingText = ""
        // Apply anything the folder did while we were holding reloads back.
        rootListView.controller.model.setReloadSuspended(false)
    }

    // Header with column sorts
    Rectangle {
        id: headerRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 26
        color: Theme.bgHeader
        border.color: Theme.borderSubtle
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            // Matches the row margin above so headers stay over their columns.
            anchors.rightMargin: (listView.contentHeight > listView.height) ? 14 : 4
            spacing: 2

            // Select All / Deselect All Toggle button
            Rectangle {
                width: 24
                height: 20
                color: selectAllMouse.containsMouse ? Theme.bgHover : "transparent"
                radius: 2

                Text {
                    anchors.centerIn: parent
                    text: rootListView.controller.model.selectedCount > 0 ? "☑" : "☐"
                    color: rootListView.controller.model.selectedCount > 0 ? Theme.accent : Theme.textMuted
                    font.pixelSize: 13
                }

                MouseArea {
                    id: selectAllMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (rootListView.controller.model.selectedCount > 0) {
                            rootListView.controller.model.deselectAll()
                        } else {
                            rootListView.controller.model.selectAll()
                        }
                    }
                }
            }

            // Name Column Header
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredWidth: 3
                Layout.fillHeight: true
                color: nameHdrMouse.containsMouse ? Theme.bgHover : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 4
                    spacing: 4

                    Text {
                        text: "Name"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: rootListView.controller.model.sortColumn === FileListModel.SortByName ? Theme.accent : Theme.textSecondary
                    }

                    Text {
                        text: rootListView.controller.model.sortAscending ? "▲" : "▼"
                        font.pixelSize: 9
                        color: Theme.accent
                        visible: rootListView.controller.model.sortColumn === FileListModel.SortByName
                    }

                    Item { Layout.fillWidth: true }
                }

                MouseArea {
                    id: nameHdrMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: rootListView.controller.model.toggleSort(FileListModel.SortByName)
                }
            }

            // Ext Column Header
            Rectangle {
                width: 50
                Layout.fillHeight: true
                color: extHdrMouse.containsMouse ? Theme.bgHover : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    spacing: 2

                    Text {
                        text: "Ext"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: rootListView.controller.model.sortColumn === FileListModel.SortByExt ? Theme.accent : Theme.textSecondary
                    }

                    Text {
                        text: rootListView.controller.model.sortAscending ? "▲" : "▼"
                        font.pixelSize: 9
                        color: Theme.accent
                        visible: rootListView.controller.model.sortColumn === FileListModel.SortByExt
                    }
                }

                MouseArea {
                    id: extHdrMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: rootListView.controller.model.toggleSort(FileListModel.SortByExt)
                }
            }

            // Size Column Header
            Rectangle {
                width: 85
                Layout.fillHeight: true
                color: sizeHdrMouse.containsMouse ? Theme.bgHover : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.rightMargin: 6
                    spacing: 2

                    Item { Layout.fillWidth: true }

                    Text {
                        text: rootListView.controller.model.sortAscending ? "▲" : "▼"
                        font.pixelSize: 9
                        color: Theme.accent
                        visible: rootListView.controller.model.sortColumn === FileListModel.SortBySize
                    }

                    Text {
                        text: "Size"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: rootListView.controller.model.sortColumn === FileListModel.SortBySize ? Theme.accent : Theme.textSecondary
                    }
                }

                MouseArea {
                    id: sizeHdrMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: rootListView.controller.model.toggleSort(FileListModel.SortBySize)
                }
            }

            // Date Column Header
            Rectangle {
                width: 120
                Layout.fillHeight: true
                color: dateHdrMouse.containsMouse ? Theme.bgHover : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    spacing: 2

                    Text {
                        text: "Date"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: rootListView.controller.model.sortColumn === FileListModel.SortByDate ? Theme.accent : Theme.textSecondary
                    }

                    Text {
                        text: rootListView.controller.model.sortAscending ? "▲" : "▼"
                        font.pixelSize: 9
                        color: Theme.accent
                        visible: rootListView.controller.model.sortColumn === FileListModel.SortByDate
                    }

                    Item { Layout.fillWidth: true }
                }

                MouseArea {
                    id: dateHdrMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: rootListView.controller.model.toggleSort(FileListModel.SortByDate)
                }
            }
        }
    }

    // Empty area / background click handler (behind delegates)
    MouseArea {
        id: bgMouseArea
        anchors.top: headerRow.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        acceptedButtons: Qt.LeftButton   // right button is handled by rightArea
        z: 0

        onPressed: (mouse) => {
            rootListView.endInlineRename()
            rootListView.controller.activate()
            listView.forceActiveFocus()
        }

    }

    // Drag and Drop Area for receiving drops from external sources (e.g. Windows Explorer)
    DropArea {
        id: listDropArea
        anchors.top: headerRow.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        onEntered: (drag) => {
            drag.accept(Qt.CopyAction)
        }

        onPositionChanged: (drag) => {
            drag.accept(Qt.CopyAction)
            updateDragHover(drag.x, drag.y)
        }

        onExited: {
            clearDragHover()
        }

        onDropped: (drop) => {
            let dest = getHoveredFolder()
            if (!dest) {
                dest = rootListView.controller.currentPath
            }
            clearDragHover()

            let paths = []
            if (drop.hasUrls && typeof appCtrl !== "undefined") {
                paths = appCtrl.urlsToPaths(drop.urls)
            } else if (typeof appCtrl !== "undefined" && appCtrl.draggedPaths && appCtrl.draggedPaths.length > 0) {
                paths = appCtrl.draggedPaths
            }

            function normalizePath(p) {
                if (!p) return ""
                return p.replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase()
            }

            let isSame = false
            if (paths.length > 0) {
                let p0 = paths[0].replace(/\\/g, "/")
                let idx = p0.lastIndexOf("/")
                let sourceDir = (idx > 0) ? p0.substring(0, idx) : p0
                if (normalizePath(dest) === normalizePath(sourceDir)) {
                    isSame = true
                }
                if (paths.some(p => normalizePath(p) === normalizePath(dest))) {
                    isSame = true
                }
            }

            if (paths && paths.length > 0 && !isSame) {
                let p0 = paths[0].replace(/\\/g, "/")
                let idx = p0.lastIndexOf("/")
                let sourceDir = (idx > 0) ? p0.substring(0, idx) : p0
                let isSamePanel = (normalizePath(sourceDir) === normalizePath(rootListView.controller.currentPath))
                let isMove = false
                if ((drop.modifiers & Qt.ShiftModifier) !== 0) {
                    isMove = true
                } else if ((drop.modifiers & Qt.ControlModifier) !== 0) {
                    isMove = false
                } else {
                    isMove = isSamePanel
                }
                drop.accept(isMove ? Qt.MoveAction : Qt.CopyAction)
                rootListView.requestDropCopy(paths, dest, isMove)
            }
            if (typeof appCtrl !== "undefined") {
                appCtrl.clearDraggedPaths()
            }
        }
    }

    // Every right-button interaction lives here, at the view level, NOT in the
    // row delegate.
    //
    // A delegate is destroyed the instant its row scrolls out of view. The
    // right-drag used to run from the delegate the drag started on, so extending
    // the selection scrolled the list, which recycled that row, which destroyed
    // the MouseArea and killed the mouse grab. The drag therefore died the moment
    // the starting row left the screen - "scrolls a little then stops". This area
    // is a child of the view and survives any amount of scrolling.
    MouseArea {
        id: rightArea
        anchors.top: headerRow.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        acceptedButtons: Qt.RightButton   // left events fall through untouched
        preventStealing: true
        z: 20

        property int pressIndex: -1
        property real pressY: 0
        property bool dragging: false

        /// Row under a point in this area's coordinates, or -1 for empty space.
        function rowAt(viewX, viewY) {
            let cp = mapToItem(listView.contentItem, viewX, viewY)
            let x = Math.max(10, Math.min(listView.width - 10, cp.x))
            return listView.indexAt(x, cp.y)
        }

        /// Same, but clamped to the ends. While dragging, a pointer past the last
        /// row means "extend to the end" rather than "nothing here".
        function dragRowAt(viewX, viewY) {
            let idx = rowAt(viewX, viewY)
            if (idx !== -1) return idx
            let cp = mapToItem(listView.contentItem, viewX, viewY)
            return (cp.y <= 0) ? 0 : listView.count - 1
        }

        onPressed: (mouse) => {
            rootListView.endInlineRename()
            rootListView.controller.activate()
            listView.forceActiveFocus()

            pressY = mouse.y
            dragging = false
            pressIndex = rowAt(mouse.x, mouse.y)
            if (pressIndex >= 0) {
                listView.currentIndex = pressIndex
            }
            rootListView.armLongPress(mapToGlobal(mouse.x, mouse.y), pressIndex)
        }

        onPositionChanged: (mouse) => {
            if (!dragging) {
                if (Math.abs(mouse.y - pressY) <= 6) return
                rootListView.cancelLongPress()
                dragging = true
                // Starting past the last row anchors on the last row.
                let anchor = (pressIndex >= 0) ? pressIndex : dragRowAt(mouse.x, pressY)
                rootListView.controller.model.beginRightDragSelection(
                    anchor, (mouse.modifiers & Qt.ShiftModifier) !== 0)
            }
            // Keep going while the pointer is held outside the list, even if it
            // stops moving - no further mouse events arrive in that case.
            rootListView.updateRightDragAutoScroll(mouse.y)

            // Exactly one thing may drive the selection at a time. Outside the
            // list the timer owns it, stepping from the current row; inside, the
            // pointer owns it. Letting both run meant the timer stepped from
            // currentIndex while the mouse snapped to the row under the cursor,
            // and the two targets disagreed - which is the jumping up and down.
            if (mouse.y < 0 || mouse.y > height) {
                return
            }
            let idx = dragRowAt(mouse.x, mouse.y)
            if (idx >= 0) rootListView.applyRightDragTo(idx)
        }

        onReleased: (mouse) => {
            rootListView.stopDragAutoScroll()
            if (dragging) {
                dragging = false
                rootListView.controller.model.endRightDragSelection()
                return
            }
            if (rootListView.longPressPending()) {
                if (pressIndex >= 0) {
                    // Short right click toggles the row, classic Commander style.
                    rootListView.cancelLongPress()
                    rootListView.controller.model.toggleSelection(pressIndex)
                } else {
                    // Empty space below the rows: show the folder context menu.
                    rootListView.releaseLongPress(mapToGlobal(mouse.x, mouse.y))
                }
            }
        }

        onCanceled: {
            rootListView.stopDragAutoScroll()
            rootListView.cancelLongPress()
            if (dragging) {
                dragging = false
                rootListView.controller.model.endRightDragSelection()
            }
        }
    }

    // File List
    ListView {
        id: listView
        objectName: "fileList"
        anchors.top: headerRow.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        model: rootListView.controller.model
        // Correct now that FilePanel is a FocusScope: this applies within its own
        // panel only, so the two panels no longer compete. A binding here would
        // not survive - forceActiveFocus() assigns to `focus` and breaks it.
        focus: true

        boundsBehavior: Flickable.StopAtBounds
        currentIndex: 0

        ScrollBar.vertical: ScrollBar {
            id: vScroll
            // AsNeeded lets the Basic style fade the bar out whenever it is not
            // being touched, so it only appeared mid-scroll. Keep it on for as
            // long as there is something to scroll, and off entirely when the
            // whole folder already fits - an inert full-height bar is just noise.
            policy: listView.contentHeight > listView.height ? ScrollBar.AlwaysOn
                                                             : ScrollBar.AlwaysOff
            width: 10

            // The default Basic look is a pale bar that barely registers on a
            // dark background; these follow the app's palette.
            contentItem: Rectangle {
                implicitWidth: 8
                radius: width / 2
                color: vScroll.pressed ? Theme.accent
                     : (vScroll.hovered ? Theme.textSecondary : Theme.borderSubtle)
            }

            background: Rectangle {
                color: Theme.bgInput
                radius: width / 2
            }
        }

        // Dedicated free space below the last item when scrolled to bottom
        footer: Item {
            id: listFooter
            width: listView.width
            height: Theme.rowHeight * 3

            MouseArea {
                id: footerMouseArea
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton   // right button is handled by rightArea
                preventStealing: true

                onPressed: (mouse) => {
                    rootListView.endInlineRename()
                    rootListView.controller.activate()
                    listView.forceActiveFocus()
                }

            }
        }

        // Empty area click handler (inside contentItem below all rows)
        MouseArea {
            id: emptyAreaMouse
            parent: listView.contentItem
            y: listView.contentHeight
            width: listView.width
            height: Math.max(listView.height, listView.contentHeight + 2000) - listView.contentHeight
            acceptedButtons: Qt.LeftButton   // right button is handled by rightArea
            preventStealing: true
            z: 0

            onPressed: (mouse) => {
                // Clicking free space with the left button cancels an inline rename
                rootListView.endInlineRename()
                rootListView.controller.activate()
                listView.forceActiveFocus()
            }

        }

        delegate: Rectangle {
            id: rowRect
            width: listView.width
            height: Theme.rowHeight
            opacity: isHidden ? 0.65 : 1.0
            color: isSelected ? (rowMouse.containsMouse ? Theme.bgSelectedHover : Theme.bgSelected) :
                   (listView.currentIndex === index && rootListView.controller.isActive ? Theme.bgHover :
                   (index % 2 === 0 ? Theme.bgPanel : Theme.bgRowAlt))

            border.color: (rootListView.dragHoverIndex === index) ? Theme.accent :
                          ((listView.currentIndex === index && rootListView.controller.isActive) ? Theme.accent : "transparent")
            border.width: (rootListView.dragHoverIndex === index) ? 2 : 1

            // Row click handler - covers whole row
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton   // right button is handled by rightArea
                preventStealing: true
                z: 5
                enabled: rootListView.editingIndex !== index

                property int lastPressX: 0
                property int lastPressY: 0
                property real pressRootX: 0
                property real pressRootY: 0
                property bool isDragActive: false
                property bool pressedOnAlreadySelected: false
                property bool pressedOnCurrent: false
                // A double click emits pressed/released for its SECOND click too.
                // Without this, that release restarts the rename timer that
                // onDoubleClicked had just stopped, and the editor opens behind
                // whatever the double click opened.
                property bool suppressRenameOnRelease: false

                onPressed: (mouse) => {
                    suppressRenameOnRelease = false
                    pressedOnAlreadySelected = isSelected && !isParent

                    if (rootListView.editingIndex >= 0 && rootListView.editingIndex !== index) {
                        rootListView.endInlineRename()
                    }
                    lastPressX = mouse.x
                    lastPressY = mouse.y
                    let pt = rowMouse.mapToItem(null, mouse.x, mouse.y)
                    pressRootX = pt.x
                    pressRootY = pt.y
                    isDragActive = false

                    let prevIndex = listView.currentIndex
                    pressedOnCurrent = (prevIndex === index)
                    // listView.onCurrentIndexChanged forwards this to the
                    // controller, so setting it again here is both redundant and
                    // another touch after the cursor has already moved.
                    listView.currentIndex = index
                    rootListView.controller.activate()
                    listView.forceActiveFocus()

                    if (mouse.button === Qt.LeftButton) {
                        if (mouse.modifiers & Qt.ShiftModifier) {
                            renameClickTimer.stop()
                            let anchor = (rootListView.selectionAnchorIndex >= 0) ? rootListView.selectionAnchorIndex : prevIndex
                            if (anchor < 0) anchor = index
                            let clearOthers = !(mouse.modifiers & Qt.ControlModifier)
                            rootListView.controller.model.selectRange(anchor, index, clearOthers)
                        } else if (mouse.modifiers & Qt.ControlModifier) {
                            renameClickTimer.stop()
                            rootListView.selectionAnchorIndex = index
                            if (!pressedOnAlreadySelected) {
                                rootListView.controller.model.toggleSelection(index)
                            }
                        } else if (!isParent) {
                            // A plain left click only moves the cursor, the same as Up/Down.
                            // Check marks are changed by the checkbox, Space, right click,
                            // or Ctrl / Shift click - never by a plain click.
                            rootListView.selectionAnchorIndex = index
                        }
                    }
                }

                onPositionChanged: (mouse) => {
                    let pt = rowMouse.mapToItem(null, mouse.x, mouse.y)
                    if (isDragActive) {
                        window.updateGlobalDrag(pt.x, pt.y, mouse.modifiers)
                    } else if (!isParent && (mouse.buttons & Qt.LeftButton)) {
                        let dx = Math.abs(pt.x - pressRootX)
                        let dy = Math.abs(pt.y - pressRootY)
                        if (dx > 8 || dy > 8) {
                            renameClickTimer.stop()
                            isDragActive = true
                            let paths = rootListView.controller.getDragPaths(index)
                            window.startGlobalDrag(rootListView.controller, paths, fileName, isDir, pt.x, pt.y, mouse.modifiers)
                        }
                    } else if (renameClickTimer.running) {
                        let dx = Math.abs(mouse.x - lastPressX)
                        let dy = Math.abs(mouse.y - lastPressY)
                        if (dx > 15 || dy > 15) {
                            renameClickTimer.stop()
                        }
                    }
                }

                onReleased: (mouse) => {
                    if (isDragActive) {
                        isDragActive = false
                        let pt = rowMouse.mapToItem(null, mouse.x, mouse.y)
                        window.endGlobalDrag(pt.x, pt.y, mouse.modifiers)
                        return
                    }
                    if (mouse.button === Qt.LeftButton) {
                        if (mouse.modifiers & Qt.ControlModifier) {
                            if (pressedOnAlreadySelected) {
                                // User pressed Ctrl on already-selected item and released without dragging: toggle off
                                rootListView.controller.model.toggleSelection(index)
                            }
                        } else if (!(mouse.modifiers & Qt.ShiftModifier) && !isParent) {
                            if (pressedOnCurrent && !suppressRenameOnRelease) {
                                // Clicked the row the cursor was already on: start an
                                // inline rename once the double-click window has passed.
                                renameClickTimer.targetIndex = index
                                renameClickTimer.start()
                            }
                        }
                    }
                }

                onCanceled: {
                    renameClickTimer.stop()
                    if (isDragActive) {
                        isDragActive = false
                        window.cancelGlobalDrag()
                    }
                }

                onDoubleClicked: (mouse) => {
                    renameClickTimer.stop()
                    suppressRenameOnRelease = true
                    if (mouse.button === Qt.LeftButton) {
                        Qt.callLater(() => {
                            rootListView.controller.openItem(index)
                        })
                    }
                }
            }

            // Selection Checkbox - elevated above rowMouse
            Rectangle {
                id: chkBox
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                radius: 2
                color: isSelected ? Theme.accent : "transparent"
                border.color: isSelected ? Theme.accent : (isParent ? "transparent" : Theme.borderSubtle)
                border.width: 1
                visible: !isParent
                z: 10

                Text {
                    anchors.centerIn: parent
                    text: isSelected ? "✓" : ""
                    font.pixelSize: 11
                    font.bold: true
                    color: "#0f172a"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    preventStealing: true
                    onPressed: {
                        listView.currentIndex = index
                        rootListView.controller.activate()
                        listView.forceActiveFocus()
                        rootListView.controller.model.toggleSelection(index)
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                // Extra room only while the scrollbar is actually shown, so the
                // Date column does not run underneath it.
                anchors.rightMargin: (listView.contentHeight > listView.height) ? 16 : 6
                spacing: 2
                z: 1

                // File Icon
                Text {
                    text: Theme.getFileIcon(fileType, isDir, isParent)
                    font.pixelSize: 14
                    Layout.preferredWidth: 20
                }

                // File Name & Inline Editor container
                Item {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 3
                    Layout.fillHeight: true

                    // Normal display when not editing
                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: fileName
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeBase
                        font.bold: isDir
                        color: isSelected ? Theme.textSelected : (isDir ? Theme.fileFolder : Theme.textPrimary)
                        elide: Text.ElideRight
                        visible: rootListView.editingIndex !== index
                    }

                    // Inline text input when editing (F2). Behind a Loader so the
                    // TextField exists only for the row actually being renamed -
                    // one per list rather than one per visible row.
                    Loader {
                        id: renameLoader
                        anchors.fill: parent
                        anchors.topMargin: 2
                        anchors.bottomMargin: 2
                        // The >= 0 guard is essential. editingIndex is -1 when
                        // nothing is being renamed, and a delegate's `index` is
                        // also -1 while it is being torn down or rebuilt - so a
                        // bare equality is TRUE during every model reset. The
                        // editor then loaded, took focus via beginEditing(), and
                        // was destroyed again, leaving focus on the delegate.
                        // That is what stole focus from the quick filter on each
                        // keystroke.
                        active: rootListView.editingIndex >= 0
                                && rootListView.editingIndex === index
                        sourceComponent: renameEditorComponent

                        // The editor is built from a Component and does NOT
                        // inherit this delegate's model context. Read the row
                        // from the model by index instead: the context roles are
                        // not reliably present while a delegate is being built
                        // or recycled, which produced "Cannot assign [undefined]".
                        onLoaded: {
                            let idx = rootListView.editingIndex
                            let row = (idx >= 0) ? rootListView.controller.model.get(idx) : null
                            // String() so these can never be undefined, whatever
                            // state the model is in when the editor is built.
                            item.rowIndex = idx
                            item.rowName = String((row && row.fileName) || rootListView.editingName || "")
                            item.rowPath = String((row && row.filePath) || "")
                            item.rowIsDir = ((row && row.isDir) === true)
                            item.beginEditing()
                        }
                    }

                    Component {
                        id: renameEditorComponent

                        TextField {
                            property string rowName: ""
                            property string rowPath: ""
                            property bool rowIsDir: false
                            property int rowIndex: -1
                            property bool hadActiveFocus: false

                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeBase
                            color: Theme.textPrimary
                            selectionColor: Theme.accent
                            selectedTextColor: "#0f172a"
                            selectByMouse: true
                            verticalAlignment: TextInput.AlignVCenter
                            padding: 4

                            background: Rectangle {
                                color: Theme.bgHeader
                                border.color: Theme.accent
                                border.width: 1.5
                                radius: 2
                            }

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    hadActiveFocus = true
                                    return
                                }
                                if (!hadActiveFocus) return
                                // A reload destroys and rebuilds every delegate, which
                                // drops focus for an instant. That is not the user
                                // clicking away, so it must not end the rename - the
                                // rebuilt editor takes focus again on its own.
                                if (rootListView.isReloadingSamePath) return
                                if (rootListView.editingIndex === rowIndex) {
                                    cancelRename()
                                }
                            }

                            onTextEdited: rootListView.editingText = text

                            /// Called by the Loader once the row's values are set.
                            function beginEditing() {
                                // Resume from what was typed if this editor is a
                                // rebuild, otherwise start from the file's name.
                                text = (rootListView.editingText !== "") ? rootListView.editingText
                                                                         : rowName
                                forceActiveFocus()
                                // Select the base name only, leaving the extension,
                                // after the field has settled.
                                Qt.callLater(() => {
                                    let dot = rowIsDir ? -1 : text.lastIndexOf(".")
                                    if (dot > 0) {
                                        select(0, dot)
                                    } else {
                                        selectAll()
                                    }
                                })
                            }

                            Keys.onReturnPressed: commitRename()
                            Keys.onEnterPressed: commitRename()
                            Keys.onEscapePressed: cancelRename()

                            function commitRename() {
                                let newName = text.trim()
                                rootListView.endInlineRename()
                                listView.forceActiveFocus()
                                if (newName !== "" && rowPath !== "" && newName !== rowName) {
                                    rootListView.controller.renameItem(rowPath, newName)
                                }
                            }

                            function cancelRename() {
                                rootListView.endInlineRename()
                                listView.forceActiveFocus()
                            }
                        }
                    }
                }

                // Extension
                Text {
                    width: 48
                    Layout.preferredWidth: 48
                    text: isDir ? "" : extension
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeSmall
                    color: isSelected ? "#e0f2fe" : Theme.textSecondary
                    elide: Text.ElideRight
                }

                // Formatted Size
                Text {
                    width: 80
                    Layout.preferredWidth: 80
                    horizontalAlignment: Text.AlignRight
                    text: formattedSize
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeSmall
                    color: isDir ? Theme.fileFolder : (isSelected ? "#e0f2fe" : Theme.textPrimary)
                }

                // Formatted Modified Date
                Text {
                    width: 115
                    Layout.preferredWidth: 115
                    text: formattedModified
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeSmall
                    color: isSelected ? "#e0f2fe" : Theme.textMuted
                    elide: Text.ElideRight
                }
            }
        }

        Connections {
            target: rootListView.controller
            function onCurrentPathChanged() {
                rootListView.endInlineRename()
                rootListView.isReloadingSamePath = false
                rootListView.selectionAnchorIndex = -1
            }
            function onFileActivated(filePath) {
                // A file was opened (double click, Enter, or F3/F4). Any rename
                // waiting on its timer must not surface behind the dialog.
                renameClickTimer.stop()
                rootListView.endInlineRename()
            }
            function onSelectItemRequested(index) {
                if (index < 0 || index >= listView.count) return
                let it = rootListView.controller.model.get(index)
                listView.currentIndex = index
                // A reload may already have queued a restoreViewState(); point it
                // at this row so it agrees instead of pulling the cursor back.
                rootListView.savedCurrentItemName = (it && it.fileName) ? it.fileName : ""
                listView.positionViewAtIndex(index, ListView.Contain)
                listView.forceActiveFocus()
            }
            function onInlineRenameRequested(index) {
                rootListView.beginInlineRename(index)
            }
            function onCurrentIndexChanged(idx) {
                if (listView.currentIndex !== idx) {
                    listView.currentIndex = idx
                }
                listView.positionViewAtIndex(idx, ListView.Contain)
            }
        }

        Connections {
            target: rootListView.controller.model
            function onBeforeDirectoryReset(isNewPath) {
                if (!isNewPath) {
                    rootListView.isReloadingSamePath = true
                    rootListView.saveViewState()
                } else {
                    rootListView.isReloadingSamePath = false
                }
            }
            function onDirectoryReset(isNewPath) {
                if (!isNewPath) {
                    rootListView.restoreViewState()
                    Qt.callLater(() => {
                        rootListView.restoreViewState()
                        rootListView.isReloadingSamePath = false
                    })
                }
            }
            function onCountChanged() {
                if (rootListView.editingName !== "") {
                    // Follow the file being renamed to its new row. If it is gone
                    // from the listing the rename is over, and endInlineRename()
                    // must run so reloads are resumed rather than left suspended.
                    let moved = rootListView.controller.model.findItemIndex(rootListView.editingName)
                    if (moved < 0) {
                        rootListView.endInlineRename()
                    } else {
                        rootListView.editingIndex = moved
                    }
                } else {
                    rootListView.editingIndex = -1
                }
                if (!rootListView.isReloadingSamePath) {
                    if (rootListView.controller.currentIndex >= 0 && rootListView.controller.currentIndex < listView.count) {
                        listView.currentIndex = rootListView.controller.currentIndex
                        listView.positionViewAtIndex(listView.currentIndex, ListView.Contain)
                    } else if (listView.count > 0) {
                        let clamped = Math.min(Math.max(0, rootListView.controller.currentIndex), listView.count - 1)
                        listView.currentIndex = clamped
                        rootListView.controller.currentIndex = clamped
                        listView.positionViewAtIndex(clamped, ListView.Contain)
                    }
                }
            }
        }

        // Keyboard navigation. These take the event before Keys.onPressed, so
        // the Shift handling has to live here.
        Keys.onUpPressed: (event) => {
            rootListView.moveCursorBy(-1, event.modifiers)
            event.accepted = true
        }

        Keys.onDownPressed: (event) => {
            rootListView.moveCursorBy(1, event.modifiers)
            event.accepted = true
        }

        Keys.onReturnPressed: {
            rootListView.controller.openItem(currentIndex)
        }

        Keys.onEnterPressed: {
            rootListView.controller.openItem(currentIndex)
        }

        Keys.onSpacePressed: {
            rootListView.controller.model.toggleSelection(currentIndex)
            if (currentIndex < count - 1) currentIndex++
        }

        onCurrentIndexChanged: {
            rootListView.controller.currentIndex = currentIndex
        }

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
                // Almost a full screen, keeping one row of overlap so you never
                // lose your place between jumps.
                let step = Math.max(1, rootListView.rowsPerPage() - 1)
                rootListView.moveCursorBy(event.key === Qt.Key_PageDown ? step : -step,
                                          event.modifiers)
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                rootListView.controller.navigateUp()
                event.accepted = true
            }
            // F2, F5, F6 and Delete are declared as window-level Shortcut
            // elements in Main.qml. Shortcuts consume the key before it reaches
            // here, so handling them again would only create a second place to
            // edit and get out of step.
        }

        // Shortcut for Select All: Ctrl+A handled at view level
        Shortcut {
            sequence: StandardKey.SelectAll
            enabled: rootListView.controller.isActive
            onActivated: rootListView.controller.model.selectAll()
        }
    }

    function ensureCurrentVisible() {
        listView.positionViewAtIndex(listView.currentIndex, ListView.Contain)
    }

    function setFocus() {
        listView.forceActiveFocus()
    }
}
