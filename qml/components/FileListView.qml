import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

Item {
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

    function startInlineRename() {
        let idx = listView.currentIndex
        if (idx >= 0 && idx < listView.count) {
            let map = rootListView.controller.model.get(idx)
            if (map && map.filePath && !map.isParent) {
                editingIndex = idx
                listView.positionViewAtIndex(idx, ListView.Contain)
            }
        }
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
            anchors.rightMargin: 4
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
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        z: 0

        property int lastPressX: 0
        property int lastPressY: 0

        Timer {
            id: bgLongRightPressTimer
            interval: 400
            repeat: false
            onTriggered: {
                let pt = bgMouseArea.mapToGlobal(bgMouseArea.lastPressX, bgMouseArea.lastPressY)
                rootListView.controller.showContextMenu(pt.x, pt.y, -1)
            }
        }

        onPressed: (mouse) => {
            rootListView.editingIndex = -1
            lastPressX = mouse.x
            lastPressY = mouse.y
            rootListView.controller.activate()
            listView.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                bgLongRightPressTimer.start()
            }
        }

        onReleased: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                if (bgLongRightPressTimer.running) {
                    bgLongRightPressTimer.stop()
                    let pt = bgMouseArea.mapToGlobal(mouse.x, mouse.y)
                    rootListView.controller.showContextMenu(pt.x, pt.y, -1)
                }
            }
        }

        onCanceled: {
            bgLongRightPressTimer.stop()
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

    // File List
    ListView {
        id: listView
        anchors.top: headerRow.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        model: rootListView.controller.model
        focus: true
        boundsBehavior: Flickable.StopAtBounds
        currentIndex: 0

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        // Dedicated free space below the last item when scrolled to bottom
        footer: Item {
            id: listFooter
            width: listView.width
            height: Theme.rowHeight * 3

            MouseArea {
                id: footerMouseArea
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: true

                property int lastPressX: 0
                property int lastPressY: 0

                Timer {
                    id: footerLongRightPressTimer
                    interval: 400
                    repeat: false
                    onTriggered: {
                        let pt = footerMouseArea.mapToGlobal(footerMouseArea.lastPressX, footerMouseArea.lastPressY)
                        rootListView.controller.showContextMenu(pt.x, pt.y, -1)
                    }
                }

                onPressed: (mouse) => {
                    rootListView.editingIndex = -1
                    lastPressX = mouse.x
                    lastPressY = mouse.y
                    rootListView.controller.activate()
                    listView.forceActiveFocus()

                    if (mouse.button === Qt.RightButton) {
                        footerLongRightPressTimer.start()
                    }
                }

                onReleased: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        if (footerLongRightPressTimer.running) {
                            footerLongRightPressTimer.stop()
                            let pt = footerMouseArea.mapToGlobal(mouse.x, mouse.y)
                            rootListView.controller.showContextMenu(pt.x, pt.y, -1)
                        }
                    }
                }

                onCanceled: {
                    footerLongRightPressTimer.stop()
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
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            preventStealing: true
            z: 0

            property int lastPressX: 0
            property int lastPressY: 0

            Timer {
                id: emptyLongRightPressTimer
                interval: 400
                repeat: false
                onTriggered: {
                    let pt = emptyAreaMouse.mapToGlobal(emptyAreaMouse.lastPressX, emptyAreaMouse.lastPressY)
                    rootListView.controller.showContextMenu(pt.x, pt.y, -1)
                }
            }

            onPressed: (mouse) => {
                // When clicked on free area with left mouse, renaming should be canceled
                rootListView.editingIndex = -1

                lastPressX = mouse.x
                lastPressY = mouse.y
                rootListView.controller.activate()
                listView.forceActiveFocus()

                if (mouse.button === Qt.RightButton) {
                    emptyLongRightPressTimer.start()
                }
            }

            onReleased: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    if (emptyLongRightPressTimer.running) {
                        emptyLongRightPressTimer.stop()
                        let pt = emptyAreaMouse.mapToGlobal(mouse.x, mouse.y)
                        rootListView.controller.showContextMenu(pt.x, pt.y, -1)
                    }
                }
            }

            onCanceled: {
                emptyLongRightPressTimer.stop()
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
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: true
                z: 5
                enabled: rootListView.editingIndex !== index

                property int lastPressX: 0
                property int lastPressY: 0
                property real pressRootX: 0
                property real pressRootY: 0
                property bool isDragActive: false
                property bool pressedOnAlreadySelected: false
                property int rightPressIndex: -1
                property bool isRightDragging: false

                Timer {
                    id: rowLongRightPressTimer
                    interval: 400
                    repeat: false
                    onTriggered: {
                        let pt = rowMouse.mapToGlobal(rowMouse.lastPressX, rowMouse.lastPressY)
                        rootListView.controller.showContextMenu(pt.x, pt.y, index)
                    }
                }

                // Delayed timer for second left click on already selected item (avoids double click)
                Timer {
                    id: renameClickTimer
                    interval: 450
                    repeat: false
                    onTriggered: {
                        if (rootListView.editingIndex === -1 &&
                            listView.currentIndex === index &&
                            isSelected &&
                            !isParent &&
                            rootListView.controller.isActive) {
                            rootListView.editingIndex = index
                            listView.positionViewAtIndex(index, ListView.Contain)
                        }
                    }
                }

                onPressed: (mouse) => {
                    pressedOnAlreadySelected = isSelected && !isParent

                    if (rootListView.editingIndex >= 0 && rootListView.editingIndex !== index) {
                        rootListView.editingIndex = -1
                    }
                    lastPressX = mouse.x
                    lastPressY = mouse.y
                    let pt = rowMouse.mapToItem(null, mouse.x, mouse.y)
                    pressRootX = pt.x
                    pressRootY = pt.y
                    isDragActive = false

                    let prevIndex = listView.currentIndex
                    listView.currentIndex = index
                    rootListView.controller.currentIndex = index
                    rootListView.controller.activate()
                    listView.forceActiveFocus()

                    if (mouse.button === Qt.RightButton) {
                        renameClickTimer.stop()
                        rightPressIndex = index
                        isRightDragging = false
                        rowLongRightPressTimer.start()
                    } else if (mouse.button === Qt.LeftButton) {
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
                            rootListView.selectionAnchorIndex = index
                            if (!isSelected) {
                                renameClickTimer.stop()
                                rootListView.controller.model.selectOnly(index)
                            }
                            // If isSelected is true, keep multi-selection intact for dragging!
                        }
                    }
                }

                onPositionChanged: (mouse) => {
                    let pt = rowMouse.mapToItem(null, mouse.x, mouse.y)
                    if (mouse.buttons & Qt.RightButton) {
                        let dx = Math.abs(mouse.x - lastPressX)
                        let dy = Math.abs(mouse.y - lastPressY)
                        if (!isRightDragging && (dx > 6 || dy > 6)) {
                            rowLongRightPressTimer.stop()
                            isRightDragging = true
                            rootListView.controller.model.beginRightDragSelection(rightPressIndex, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                        }
                        if (isRightDragging) {
                            let contentPos = rowMouse.mapToItem(listView.contentItem, mouse.x, mouse.y)
                            let clampedX = Math.max(10, Math.min(listView.width - 10, contentPos.x))
                            let targetIdx = listView.indexAt(clampedX, contentPos.y)
                            if (targetIdx === -1) {
                                if (contentPos.y <= 0) targetIdx = 0
                                else if (contentPos.y >= listView.contentHeight) targetIdx = listView.count - 1
                                else targetIdx = Math.max(0, Math.min(listView.count - 1, Math.floor(contentPos.y / Theme.rowHeight)))
                            }
                            if (targetIdx >= 0 && targetIdx < listView.count) {
                                rootListView.controller.model.updateRightDragSelection(targetIdx)
                                listView.currentIndex = targetIdx
                                rootListView.controller.currentIndex = targetIdx
                                listView.positionViewAtIndex(targetIdx, ListView.Contain)
                            }
                            return
                        }
                    } else if (isDragActive) {
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
                    if (mouse.button === Qt.RightButton) {
                        if (isRightDragging) {
                            isRightDragging = false
                            rootListView.controller.model.endRightDragSelection()
                            return
                        }
                        if (rowLongRightPressTimer.running) {
                            rowLongRightPressTimer.stop()
                            // Short right click: toggle selection (classic Commander style)
                            rootListView.controller.model.toggleSelection(index)
                        }
                    } else if (mouse.button === Qt.LeftButton) {
                        if (mouse.modifiers & Qt.ControlModifier) {
                            if (pressedOnAlreadySelected) {
                                // User pressed Ctrl on already-selected item and released without dragging: toggle off
                                rootListView.controller.model.toggleSelection(index)
                            }
                        } else if (!(mouse.modifiers & Qt.ShiftModifier) && !isParent) {
                            if (pressedOnAlreadySelected) {
                                if (rootListView.controller.selectedItemsCount > 1) {
                                    // Released without dragging on a multi-selection: select only this item
                                    rootListView.controller.model.selectOnly(index)
                                } else {
                                    // Second click on uniquely selected item: schedule rename after double-click window
                                    renameClickTimer.start()
                                }
                            }
                        }
                    }
                }

                onCanceled: {
                    rowLongRightPressTimer.stop()
                    renameClickTimer.stop()
                    if (isRightDragging) {
                        isRightDragging = false
                        rootListView.controller.model.endRightDragSelection()
                    }
                    if (isDragActive) {
                        isDragActive = false
                        window.cancelGlobalDrag()
                    }
                }

                onDoubleClicked: (mouse) => {
                    renameClickTimer.stop()
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
                anchors.rightMargin: 6
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

                    // Inline text input when editing (F2)
                    TextField {
                        id: inlineRenameInput
                        anchors.fill: parent
                        anchors.topMargin: 2
                        anchors.bottomMargin: 2
                        visible: rootListView.editingIndex === index
                        text: (typeof fileName !== "undefined" && fileName) ? fileName : ""
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

                        property bool hadActiveFocus: false

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                hadActiveFocus = true
                            } else if (hadActiveFocus && rootListView.editingIndex === index) {
                                cancelRename()
                            }
                        }

                        onVisibleChanged: {
                            if (visible && typeof fileName !== "undefined" && fileName) {
                                hadActiveFocus = false
                                text = String(fileName)
                                forceActiveFocus()
                                Qt.callLater(() => {
                                    let dot = isDir ? -1 : text.lastIndexOf(".")
                                    if (dot > 0) {
                                        select(0, dot)
                                    } else {
                                        selectAll()
                                    }
                                })
                            } else {
                                hadActiveFocus = false
                            }
                        }

                        Keys.onReturnPressed: commitRename()
                        Keys.onEnterPressed: commitRename()
                        Keys.onEscapePressed: cancelRename()

                        function commitRename() {
                            let newName = text.trim()
                            let oldPath = (typeof filePath !== "undefined" && filePath) ? filePath : ""
                            let currentName = (typeof fileName !== "undefined" && fileName) ? fileName : ""
                            rootListView.editingIndex = -1
                            listView.forceActiveFocus()
                            if (newName !== "" && oldPath !== "" && newName !== currentName) {
                                rootListView.controller.renameItem(oldPath, newName)
                            }
                        }

                        function cancelRename() {
                            rootListView.editingIndex = -1
                            listView.forceActiveFocus()
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
                rootListView.editingIndex = -1
                rootListView.isReloadingSamePath = false
                rootListView.selectionAnchorIndex = -1
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
                rootListView.editingIndex = -1
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

        // Keyboard navigation
        Keys.onUpPressed: {
            if (currentIndex > 0) currentIndex--
        }

        Keys.onDownPressed: {
            if (currentIndex < count - 1) currentIndex++
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
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down) && (event.modifiers & Qt.ShiftModifier)) {
                if (rootListView.selectionAnchorIndex < 0) {
                    rootListView.selectionAnchorIndex = listView.currentIndex
                }
                let target = (event.key === Qt.Key_Up) ? Math.max(0, listView.currentIndex - 1) : Math.min(listView.count - 1, listView.currentIndex + 1)
                listView.currentIndex = target
                rootListView.controller.model.selectRange(rootListView.selectionAnchorIndex, target, !(event.modifiers & Qt.ControlModifier))
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                rootListView.controller.navigateUp()
                event.accepted = true
            } else if (event.key === Qt.Key_F2) {
                rootListView.startInlineRename()
                event.accepted = true
            } else if (event.key === Qt.Key_Delete) {
                let isShift = (event.modifiers & Qt.ShiftModifier) !== 0
                rootListView.requestDelete(isShift)
                event.accepted = true
            } else if (event.key === Qt.Key_F5) {
                rootListView.requestCopy()
                event.accepted = true
            } else if (event.key === Qt.Key_F6) {
                rootListView.requestMove()
                event.accepted = true
            }
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
