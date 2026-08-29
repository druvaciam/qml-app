import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander
import "components"

ApplicationWindow {
    id: window
    width: 1280
    height: 780
    minimumWidth: 850
    minimumHeight: 520
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "QML Commander - Dual-Pane File Manager"
    color: Theme.bgApp

    Timer {
        id: saveGeometryTimer
        interval: 600
        repeat: false
        onTriggered: {
            appCtrl.saveWindowGeometry(window)
        }
    }

    onWidthChanged: saveGeometryTimer.restart()
    onHeightChanged: saveGeometryTimer.restart()
    onXChanged: saveGeometryTimer.restart()
    onYChanged: saveGeometryTimer.restart()
    onVisibilityChanged: saveGeometryTimer.restart()

    Component.onCompleted: {
        appCtrl.restoreWindowGeometry(window)
        appCtrl.setupTrayIcon(window)
    }

    onClosing: {
        saveGeometryTimer.stop()
        appCtrl.saveWindowGeometry(window)
        appCtrl.saveSession()
    }

    AppController {
        id: appCtrl

        onRequestPreviewFile: (filePath) => {
            previewDialog.open(filePath, false)
        }

        // showMessageRequested is handled by the Connections block further down.
    }

    // Global Drag State (window-level coordinate system, never clipped)
    property bool isDragging: false
    property bool dragIsMove: false
    property bool dragIsValidTarget: false
    property int dragModifiers: 0
    property var dragSourceController: null
    property var draggedPaths: []
    property string dragTitle: ""
    property bool dragIsDir: false
    property int dragCount: 0
    property real dragMouseX: 0
    property real dragMouseY: 0

    function startGlobalDrag(sourceCtrl, paths, title, isDir, startRootX, startRootY, modifiers) {
        if (!paths || paths.length === 0) return
        isDragging = true
        dragIsValidTarget = false
        dragSourceController = sourceCtrl
        draggedPaths = paths
        dragTitle = title
        dragIsDir = isDir
        dragCount = paths.length
        dragModifiers = (typeof modifiers !== "undefined") ? modifiers : 0
        updateGlobalDrag(startRootX, startRootY, modifiers)
    }

    function normalizePath(p) {
        if (!p) return ""
        return p.replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase()
    }

    /// Which pane is under (rootX, rootY), if either. Returns the pane item and
    /// its controller so callers do not repeat the hit test.
    function paneAt(rootX, rootY) {
        let l = leftPanel.mapFromItem(null, rootX, rootY)
        if (l.x >= 0 && l.x <= leftPanel.width && l.y >= 0 && l.y <= leftPanel.height) {
            return { pane: leftPanel, other: rightPanel, controller: appCtrl.leftPanel }
        }
        let r = rightPanel.mapFromItem(null, rootX, rootY)
        if (r.x >= 0 && r.x <= rightPanel.width && r.y >= 0 && r.y <= rightPanel.height) {
            return { pane: rightPanel, other: leftPanel, controller: appCtrl.rightPanel }
        }
        return null
    }

    /// Shift forces Move, Ctrl forces Copy, otherwise dragging within one pane
    /// moves and dragging across panes copies.
    function dropIsMove(modifiers, isSamePanel) {
        if ((modifiers & Qt.ShiftModifier) !== 0) return true
        if ((modifiers & Qt.ControlModifier) !== 0) return false
        return isSamePanel
    }

    function dragSourceDir() {
        if (dragSourceController && dragSourceController.currentPath) {
            return dragSourceController.currentPath
        }
        if (draggedPaths.length > 0) {
            let p0 = draggedPaths[0].replace(/\\/g, "/")
            let idx = p0.lastIndexOf("/")
            if (idx > 0) return p0.substring(0, idx)
        }
        return ""
    }

    /// A drop is pointless if it lands in the folder the items already live in,
    /// or onto one of the dragged items itself.
    function dropIsRedundant(dest, sourceDir) {
        if (normalizePath(dest) === normalizePath(sourceDir)) return true
        return draggedPaths.some(p => normalizePath(p) === normalizePath(dest))
    }

    function updateGlobalDrag(rootX, rootY, modifiers) {
        if (!isDragging) return
        dragMouseX = rootX
        dragMouseY = rootY
        if (typeof modifiers !== "undefined") {
            dragModifiers = modifiers
        }

        let hit = paneAt(rootX, rootY)
        let isSamePanel = false

        if (hit) {
            let listLocal = hit.pane.fileListView.mapFromItem(null, rootX, rootY)
            hit.pane.fileListView.updateDragHover(listLocal.x, listLocal.y)
            hit.other.fileListView.clearDragHover()

            let hoveredFolder = hit.pane.fileListView.getHoveredFolder()
            let dest = hoveredFolder ? hoveredFolder : hit.controller.currentPath
            let redundant = dropIsRedundant(dest, dragSourceDir())

            hit.pane.isDragTarget = !redundant
            hit.other.isDragTarget = false
            isSamePanel = (dragSourceController === hit.controller)
            dragIsValidTarget = !redundant
        } else {
            leftPanel.isDragTarget = false
            rightPanel.isDragTarget = false
            leftPanel.fileListView.clearDragHover()
            rightPanel.fileListView.clearDragHover()
            dragIsValidTarget = false
        }

        dragIsMove = dropIsMove(dragModifiers, isSamePanel)
    }

    function endGlobalDrag(rootX, rootY, modifiers) {
        if (!isDragging) return
        let paths = draggedPaths
        if (typeof modifiers !== "undefined") {
            dragModifiers = modifiers
        }

        let sourceDir = dragSourceDir()
        let hit = paneAt(rootX, rootY)

        let dest = ""
        let isSamePanel = false
        if (hit) {
            let folder = hit.pane.fileListView.getHoveredFolder()
            dest = folder ? folder : hit.controller.currentPath
            isSamePanel = (dragSourceController === hit.controller)
        }

        let redundant = dest ? dropIsRedundant(dest, sourceDir) : true
        let isMove = dropIsMove(dragModifiers, isSamePanel)

        cancelGlobalDrag()

        if (redundant) {
            // Dropped back into the source folder or onto one of the dragged
            // items, so there is nothing to do
            return
        }

        if (dest && paths.length > 0) {
            executeDropOperation(paths, dest, isMove)
        }
    }

    function cancelGlobalDrag() {
        isDragging = false
        dragIsMove = false
        dragIsValidTarget = false
        dragModifiers = 0
        dragSourceController = null
        draggedPaths = []
        leftPanel.isDragTarget = false
        rightPanel.isDragTarget = false
        leftPanel.fileListView.clearDragHover()
        rightPanel.fileListView.clearDragHover()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top App Header / Title Bar
        Rectangle {
            id: titleBar
            Layout.fillWidth: true
            height: 42
            color: Theme.bgHeader
            border.color: Theme.borderSubtle
            border.width: 1

            // Window Dragging & Double-Click Maximize on titleBar background
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onPressed: (mouse) => {
                    if (mouse.button === Qt.LeftButton) {
                        window.startSystemMove()
                    }
                }
                onDoubleClicked: (mouse) => {
                    if (mouse.button === Qt.LeftButton) {
                        if (window.visibility === Window.Maximized) {
                            window.showNormal()
                        } else {
                            window.showMaximized()
                        }
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 0
                spacing: 12
                z: 2

                // Brand
                RowLayout {
                    spacing: 8
                    Text {
                        text: "⚡"
                        font.pixelSize: 18
                    }

                    Text {
                        text: "QML Commander"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: Theme.textPrimary
                    }

                    Rectangle {
                        height: 18
                        width: tagText.width + 10
                        radius: 9
                        color: Theme.bgSelected

                        Text {
                            id: tagText
                            anchors.centerIn: parent
                            text: "Qt 6 C++ / QML"
                            font.family: Theme.fontFamily
                            font.pixelSize: 10
                            font.bold: true
                            color: Theme.accent
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Quick Action Buttons
                RowLayout {
                    spacing: 6

                    // Swap Panes Button
                    Rectangle {
                        height: 28
                        width: swapLabel.width + 16
                        radius: Theme.radiusSmall
                        color: topSwapMouse.containsMouse ? Theme.bgHover : Theme.bgPanel
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Row {
                            id: swapLabel
                            anchors.centerIn: parent
                            spacing: 4
                            Text { text: "⇄"; font.pixelSize: 12; color: Theme.accent }
                            Text { text: "Swap (Ctrl+U)"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall; color: Theme.textPrimary }
                        }

                        MouseArea {
                            id: topSwapMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: appCtrl.swapPanes()
                        }
                    }

                    // Equalize Panes Button
                    Rectangle {
                        height: 28
                        width: eqLabel.width + 16
                        radius: Theme.radiusSmall
                        color: topEqMouse.containsMouse ? Theme.bgHover : Theme.bgPanel
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Row {
                            id: eqLabel
                            anchors.centerIn: parent
                            spacing: 4
                            Text { text: "="; font.pixelSize: 12; color: Theme.accent }
                            Text { text: "Equalize (Ctrl+E)"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall; color: Theme.textPrimary }
                        }

                        MouseArea {
                            id: topEqMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: appCtrl.equalizePanes()
                        }
                    }

                    // Terminal Button
                    Rectangle {
                        height: 28
                        width: termLabel.width + 16
                        radius: Theme.radiusSmall
                        color: topTermMouse.containsMouse ? Theme.bgHover : Theme.bgPanel
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Row {
                            id: termLabel
                            anchors.centerIn: parent
                            spacing: 4
                            Text { text: "⌨"; font.pixelSize: 12; color: Theme.accent }
                            Text { text: "Terminal"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall; color: Theme.textPrimary }
                        }

                        MouseArea {
                            id: topTermMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: appCtrl.openTerminalInActivePanel()
                        }
                    }

                    // Refresh Button
                    Rectangle {
                        width: 28
                        height: 28
                        radius: Theme.radiusSmall
                        color: topRefrMouse.containsMouse ? Theme.bgHover : Theme.bgPanel
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "↻"
                            font.pixelSize: 14
                            color: Theme.textPrimary
                        }

                        MouseArea {
                            id: topRefrMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: appCtrl.refreshAll()
                            ToolTip.visible: containsMouse
                            ToolTip.text: "Refresh All (Ctrl+R)"
                        }
                    }
                }

                // Separator
                Rectangle {
                    width: 1
                    height: 20
                    color: Theme.borderSubtle
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: 4
                    Layout.rightMargin: 2
                }

                // Custom Window Control Buttons (Min, Max/Restore, Close)
                Row {
                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                    spacing: 0
                    height: 42

                    // Minimize
                    Rectangle {
                        width: 44
                        height: 42
                        color: minMouse.containsMouse ? Theme.bgHover : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "—"
                            font.pixelSize: 11
                            color: minMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary
                        }

                        MouseArea {
                            id: minMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.showMinimized()
                            ToolTip.visible: containsMouse
                            ToolTip.text: "Minimize"
                        }
                    }

                    // Maximize / Restore
                    Rectangle {
                        width: 44
                        height: 42
                        color: maxMouse.containsMouse ? Theme.bgHover : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: (window.visibility === Window.Maximized) ? "❐" : "□"
                            font.pixelSize: (window.visibility === Window.Maximized) ? 12 : 13
                            color: maxMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary
                        }

                        MouseArea {
                            id: maxMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (window.visibility === Window.Maximized) {
                                    window.showNormal()
                                } else {
                                    window.showMaximized()
                                }
                            }
                            ToolTip.visible: containsMouse
                            ToolTip.text: (window.visibility === Window.Maximized) ? "Restore Down" : "Maximize"
                        }
                    }

                    // Close
                    Rectangle {
                        width: 46
                        height: 42
                        color: closeMouse.containsMouse ? "#e11d48" : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            font.pixelSize: 12
                            color: closeMouse.containsMouse ? "#ffffff" : Theme.textSecondary
                        }

                        MouseArea {
                            id: closeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.close()
                            ToolTip.visible: containsMouse
                            ToolTip.text: "Close"
                        }
                    }
                }
            }
        }

        // Dual Pane SplitView
        SplitView {
            id: splitView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 6
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 6
                color: SplitHandle.pressed ? Theme.accent : (SplitHandle.hovered ? Theme.bgHover : "transparent")

                Rectangle {
                    anchors.centerIn: parent
                    width: 2
                    height: 30
                    radius: 1
                    color: Theme.borderSubtle
                }
            }

            // Left File Panel
            FilePanel {
                id: leftPanel
                SplitView.fillWidth: true
                SplitView.preferredWidth: parent.width / 2
                SplitView.minimumWidth: 320
                controller: appCtrl.leftPanel
                onRequestDelete: (permanent) => executeDelete(permanent)
                onRequestCopy: executeCopy()
                onRequestMove: executeMove()
                onRequestDropCopy: (paths, destination, isMove) => executeDropOperation(paths, destination, isMove)
            }

            // Right File Panel
            FilePanel {
                id: rightPanel
                SplitView.fillWidth: true
                SplitView.preferredWidth: parent.width / 2
                SplitView.minimumWidth: 320
                controller: appCtrl.rightPanel
                onRequestDelete: (permanent) => executeDelete(permanent)
                onRequestCopy: executeCopy()
                onRequestMove: executeMove()
                onRequestDropCopy: (paths, destination, isMove) => executeDropOperation(paths, destination, isMove)
            }
        }

        // Bottom Total Commander Function Key Bar
        FunctionBar {
            Layout.fillWidth: true

            onRequestView: executeView()
            onRequestEdit: executeEdit()
            onRequestCopy: executeCopy()
            onRequestMove: executeMove()
            onRequestNewFolder: {
                cancelPendingRename()
                newFolderDialog.open()
            }
            onRequestDelete: executeDelete(false)
            onRequestSwap: appCtrl.swapPanes()
            onRequestTerminal: appCtrl.openTerminalInActivePanel()
            onRequestRefresh: appCtrl.refreshAll()
        }
    }

    // Helper functions for action dispatch
    /// Drop any rename that is open or merely armed, in both panes. Called
    /// before opening a dialog so the editor cannot surface behind it and take
    /// the focus that Escape depends on.
    function cancelPendingRename() {
        leftPanel.fileListView.endInlineRename()
        rightPanel.fileListView.endInlineRename()
    }

    function executeView() {
        cancelPendingRename()
        let targets = appCtrl.getActiveOrSelectedPaths()
        if (targets.length > 0) {
            previewDialog.open(targets[0], false)
        }
    }

    function executeEdit() {
        cancelPendingRename()
        let targets = appCtrl.getActiveOrSelectedPaths()
        if (targets.length > 0) {
            previewDialog.open(targets[0], true)
        }
    }

    function executeCopy() {
        cancelPendingRename()
        let targets = appCtrl.getActiveOrSelectedPaths()
        if (targets.length > 0) {
            let defaultDest = (appCtrl.targetPanel && appCtrl.targetPanel.currentPath) ? appCtrl.targetPanel.currentPath : ""
            copyMoveDialog.open(targets, defaultDest, false)
        } else {
            appCtrl.copySelected()
        }
    }

    function executeDropOperation(paths, destination, isMove = false) {
        if (paths && paths.length > 0 && destination) {
            copyMoveDialog.open(paths, destination, isMove)
        }
    }

    function executeMove() {
        cancelPendingRename()
        let targets = appCtrl.getActiveOrSelectedPaths()
        if (targets.length > 0) {
            let defaultDest = (appCtrl.targetPanel && appCtrl.targetPanel.currentPath) ? appCtrl.targetPanel.currentPath : ""
            copyMoveDialog.open(targets, defaultDest, true)
        } else {
            appCtrl.moveSelected()
        }
    }

    function executeRename() {
        if (appCtrl.activePanel === appCtrl.leftPanel) {
            leftPanel.fileListView.startInlineRename()
        } else {
            rightPanel.fileListView.startInlineRename()
        }
    }

    function executeDelete(permanent = false) {
        cancelPendingRename()
        let targets = appCtrl.getActiveOrSelectedPaths()
        if (targets.length > 0) {
            confirmDeleteDialog.open(targets, permanent)
        } else {
            appCtrl.deleteSelected([], permanent)
        }
    }

    function restoreActiveFocus() {
        if (appCtrl.activePanelIndex === 0) {
            leftPanel.fileListView.setFocus()
        } else {
            rightPanel.fileListView.setFocus()
        }
    }

    // Modals & Dialogs
    OperationProgressModal {
        fileOps: appCtrl.fileOps
    }

    CopyMoveDialog {
        id: copyMoveDialog
        onAccepted: (items, destination, isMove) => {
            if (isMove) {
                appCtrl.moveSelected(items, destination)
            } else {
                appCtrl.copySelected(items, destination)
            }
        }
        onClosed: restoreActiveFocus()
    }

    FilePreviewDialog {
        id: previewDialog
        previewService: appCtrl.preview
        onClosed: restoreActiveFocus()
    }

    NewFolderDialog {
        id: newFolderDialog
        onAccepted: (folderName) => {
            appCtrl.createFolder(folderName)
        }
        onClosed: restoreActiveFocus()
    }

    ConfirmDeleteDialog {
        id: confirmDeleteDialog
        onAccepted: (items, permanent) => {
            appCtrl.deleteSelected(items, permanent)
        }
        onClosed: restoreActiveFocus()
    }

    // Simple Alert / Info Modal
    Rectangle {
        id: messageDialog
        property string title: ""
        property string messageText: ""
        property bool isOpen: false

        function open() {
            isOpen = true
            forceActiveFocus()
        }
        function close() {
            isOpen = false
            restoreActiveFocus()
        }

        visible: isOpen
        anchors.fill: parent
        color: "#a0000000"
        z: 120
        focus: isOpen

        // Without this the panels underneath stay clickable while an error is
        // on screen. CopyMoveDialog and ConfirmDeleteDialog both do the same.
        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        Rectangle {
            width: 380
            height: 160
            anchors.centerIn: parent
            radius: Theme.radiusLarge
            color: Theme.bgDialog
            border.color: Theme.accent
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Text {
                    text: messageDialog.title
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                    color: Theme.textPrimary
                }

                Text {
                    text: messageDialog.messageText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: 80
                        height: 30
                        radius: Theme.radiusSmall
                        color: msgOkMouse.containsMouse ? Theme.accentHover : Theme.accent
                        Text {
                            anchors.centerIn: parent
                            text: "OK"
                            font.bold: true
                            color: "#0f172a"
                        }
                        MouseArea {
                            id: msgOkMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: messageDialog.close()
                        }
                    }
                }
            }
        }

        Keys.onEscapePressed: (event) => { messageDialog.close(); event.accepted = true }
        Keys.onReturnPressed: (event) => { messageDialog.close(); event.accepted = true }
        Keys.onEnterPressed: (event) => { messageDialog.close(); event.accepted = true }
    }

    Connections {
        target: appCtrl
        function onShowMessageRequested(title, message) {
            messageDialog.title = title
            messageDialog.messageText = message
            messageDialog.open()
        }
    }

    // Global Shortcuts
    Shortcut {
        sequence: "Tab"
        onActivated: {
            appCtrl.toggleActivePanel()
            if (appCtrl.activePanelIndex === 0) {
                leftPanel.fileListView.setFocus()
            } else {
                rightPanel.fileListView.setFocus()
            }
        }
    }

    Shortcut {
        sequence: "F2"
        onActivated: executeRename()
    }

    Shortcut {
        sequence: "F3"
        onActivated: executeView()
    }

    Shortcut {
        sequence: "F4"
        onActivated: executeEdit()
    }

    Shortcut {
        sequence: "F5"
        onActivated: executeCopy()
    }

    Shortcut {
        sequence: "F6"
        onActivated: executeMove()
    }

    Shortcut {
        sequence: "F7"
        onActivated: {
            cancelPendingRename()
            newFolderDialog.open()
        }
    }

    Shortcut {
        sequence: "F8"
        onActivated: executeDelete(false)
    }

    Shortcut {
        sequence: "Delete"
        onActivated: executeDelete(false)
    }

    Shortcut {
        sequence: "Shift+Delete"
        onActivated: executeDelete(true)
    }

    Shortcut {
        sequence: "Ctrl+U"
        onActivated: appCtrl.swapPanes()
    }

    Shortcut {
        sequence: "Ctrl+E"
        onActivated: appCtrl.equalizePanes()
    }

    Shortcut {
        sequence: "Ctrl+R"
        onActivated: appCtrl.refreshAll()
    }

    Shortcut {
        sequence: "Ctrl+T"
        onActivated: appCtrl.openTerminalInActivePanel()
    }

    Shortcut {
        sequence: "Ctrl+H"
        onActivated: appCtrl.toggleHiddenFiles()
    }

    Shortcut {
        sequence: "Ctrl+F"
        onActivated: {
            if (appCtrl.activePanelIndex === 0) {
                leftPanel.focusFilter()
            } else {
                rightPanel.focusFilter()
            }
        }
    }

    function isInputFocused() {
        let item = window.activeFocusItem
        if (item && (item.hasOwnProperty("selectedText") || item.hasOwnProperty("cursorPosition"))) {
            return true
        }
        if ((leftPanel && leftPanel.fileListView && leftPanel.fileListView.editingIndex !== -1) ||
            (rightPanel && rightPanel.fileListView && rightPanel.fileListView.editingIndex !== -1)) {
            return true
        }
        return (copyMoveDialog.visible || previewDialog.visible || 
                newFolderDialog.visible ||
                confirmDeleteDialog.visible || messageDialog.visible)
    }

    Shortcut {
        sequences: ["Ctrl+C"]
        enabled: !isInputFocused()
        onActivated: appCtrl.copyToClipboard()
    }

    Shortcut {
        sequences: ["Ctrl+X"]
        enabled: !isInputFocused()
        onActivated: appCtrl.cutToClipboard()
    }

    Shortcut {
        sequences: ["Ctrl+V"]
        enabled: !isInputFocused()
        onActivated: appCtrl.pasteFromClipboard()
    }

    // Dynamic modifier updates while dragging
    Item {
        anchors.fill: parent
        focus: window.isDragging
        z: 99998
        visible: window.isDragging
        Keys.onPressed: (event) => {
            if (window.isDragging) {
                window.updateGlobalDrag(window.dragMouseX, window.dragMouseY, event.modifiers)
            }
        }
        Keys.onReleased: (event) => {
            if (window.isDragging) {
                window.updateGlobalDrag(window.dragMouseX, window.dragMouseY, event.modifiers)
            }
        }
    }

    // Floating Drag Badge - positioned at top-level window coordinates, never clipped
    Rectangle {
        id: globalDragBadge
        z: 99999
        visible: window.isDragging
        x: window.dragMouseX + 16
        y: window.dragMouseY + 16
        width: Math.min(420, dragBadgeLayout.implicitWidth + 24)
        height: 34
        color: Theme.bgHeader
        border.color: !window.dragIsValidTarget ? "#64748b" : (window.dragIsMove ? "#f59e0b" : Theme.accent)
        border.width: 1.5
        radius: 6
        opacity: window.dragIsValidTarget ? 0.96 : 0.85

        RowLayout {
            id: dragBadgeLayout
            anchors.fill: parent
            anchors.margins: 6
            spacing: 6

            // Action Tag badge (Copy, Move, or Gray Cancel when invalid)
            Rectangle {
                width: actionTagText.implicitWidth + 10
                height: 20
                radius: 3
                color: !window.dragIsValidTarget ? "#475569" : (window.dragIsMove ? "#f59e0b" : Theme.accent)

                Text {
                    id: actionTagText
                    anchors.centerIn: parent
                    text: !window.dragIsValidTarget ? "CANCEL" : (window.dragIsMove ? "MOVE" : "COPY")
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    font.bold: true
                    color: !window.dragIsValidTarget ? "#cbd5e1" : "#ffffff"
                }
            }

            Text {
                text: window.dragIsDir ? "📁" : "📄"
                font.pixelSize: 14
            }

            Text {
                Layout.fillWidth: true
                text: {
                    if (window.dragCount > 1) {
                        return window.dragTitle + " +" + (window.dragCount - 1)
                    }
                    return window.dragTitle
                }
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSmall
                font.bold: true
                color: Theme.textPrimary
                elide: Text.ElideRight
            }
        }
    }

    // Resize Handles for Frameless Window (active when not maximized)
    Item {
        id: resizeHandles
        anchors.fill: parent
        visible: window.visibility !== Window.Maximized
        z: 99990

        // 4 Edges
        MouseArea {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 6
            cursorShape: Qt.SizeVerCursor
            onPressed: window.startSystemResize(Qt.TopEdge)
        }
        MouseArea {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 6
            cursorShape: Qt.SizeVerCursor
            onPressed: window.startSystemResize(Qt.BottomEdge)
        }
        MouseArea {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 6
            cursorShape: Qt.SizeHorCursor
            onPressed: window.startSystemResize(Qt.LeftEdge)
        }
        MouseArea {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 6
            cursorShape: Qt.SizeHorCursor
            onPressed: window.startSystemResize(Qt.RightEdge)
        }

        // 4 Corners
        MouseArea {
            anchors.left: parent.left
            anchors.top: parent.top
            width: 12
            height: 12
            cursorShape: Qt.SizeFDiagCursor
            onPressed: window.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
        }
        MouseArea {
            anchors.right: parent.right
            anchors.top: parent.top
            width: 12
            height: 12
            cursorShape: Qt.SizeBDiagCursor
            onPressed: window.startSystemResize(Qt.TopEdge | Qt.RightEdge)
        }
        MouseArea {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: 12
            height: 12
            cursorShape: Qt.SizeBDiagCursor
            onPressed: window.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
        }
        MouseArea {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 12
            height: 12
            cursorShape: Qt.SizeFDiagCursor
            onPressed: window.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
        }
    }

    // Outer subtle border when not maximized
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.borderSubtle
        border.width: (window.visibility === Window.Maximized) ? 0 : 1
        z: 99980
        enabled: false
    }
}
