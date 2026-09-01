import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

Rectangle {
    id: root
    objectName: "previewDialog"
    required property FilePreviewService previewService

    property string filePath: ""
    property var fileData: ({})
    property bool isOpen: false
    property bool isEditMode: false
    property string saveError: ""
    property string saveNotice: ""

    visible: isOpen
    anchors.fill: parent
    color: "#b0000000"
    z: 110
    // Escape is handled by Keys.onEscapePressed below, which only fires while
    // this item holds focus. Without this it depends on whatever happened to
    // have focus when the dialog opened.
    focus: isOpen

    signal closed()

    function open(path, editMode = false) {
        filePath = path
        isEditMode = editMode
        saveError = ""
        saveNotice = ""
        fileData = previewService.loadPreview(path)
        if (fileData.isText) {
            editorText.text = fileData.content || ""
        }
        isOpen = true
        root.forceActiveFocus()
        root.focus = true

        // The caret goes into the text for viewing as well as editing. In view
        // mode the editor is read-only, so nothing can be typed - but Page Up,
        // Page Down and the arrows all need something focused to act on, and
        // with focus left on the dialog root they did nothing at all.
        if (root.fileData?.isText) {
            Qt.callLater(() => {
                editorText.forceActiveFocus()
                editorText.cursorPosition = 0
            })
        }
    }

    /// What Tab cycles through. Only the items that are actually on screen:
    /// Save is hidden when the file is being viewed rather than edited, and
    /// tabbing onto an invisible button would look like Tab had stopped
    /// working.
    function focusRing() {
        let ring = []
        if (editorText.visible) ring.push(editorText)
        if (saveButton.visible) ring.push(saveButton)
        if (closeButton.visible) ring.push(closeButton)
        return ring
    }

    function stepFocus(delta) {
        const ring = root.focusRing()
        if (ring.length === 0) {
            return
        }
        let at = -1
        for (let i = 0; i < ring.length; ++i) {
            if (ring[i].activeFocus) {
                at = i
                break
            }
        }
        const next = (at < 0) ? (delta > 0 ? 0 : ring.length - 1)
                              : (at + delta + ring.length) % ring.length
        ring[next].forceActiveFocus()
    }

    /// Enter and Space press whichever button has focus. In the editor they do
    /// what they always did - a newline and a space.
    function activateFocused() {
        if (saveButton.activeFocus) {
            root.save(true)
        } else if (closeButton.activeFocus) {
            root.close()
        }
    }

    /// True while the file can actually be written: edit mode, real text, and
    /// not one of the large files that are loaded only in part.
    readonly property bool canSave: Boolean(isOpen && isEditMode
                                            && fileData?.isText
                                            && !fileData?.isTruncated)

    /// Save the editor's contents. closeAfter is true for the Save button and
    /// false for Ctrl+S, which keeps the file open so editing can continue.
    function save(closeAfter) {
        if (!canSave) return false
        if (!previewService.saveTextFile(filePath, editorText.text)) {
            saveNotice = ""
            saveError = "Could not save — the file is read-only or in use. Your changes are still here."
            return false
        }
        saveError = ""
        // The panels update themselves: FilePreviewService emits fileSaved and
        // AppController refreshes that one row.
        if (closeAfter) {
            close()
        } else {
            saveNotice = "Saved"
            saveNoticeTimer.restart()
            editorText.forceActiveFocus()
        }
        return true
    }

    Timer {
        id: saveNoticeTimer
        interval: 2000
        repeat: false
        onTriggered: root.saveNotice = ""
    }

    Shortcut {
        sequences: [StandardKey.Save]
        enabled: root.canSave
        onActivated: root.save(false)
    }

    // Escape as a Shortcut, not a key handler.
    //
    // Key handlers only fire for whichever item happens to hold focus, and this
    // dialog moves focus around: opening in edit mode focuses the editor, and
    // Ctrl+S focuses it again so typing can continue. Every attempt to place the
    // handler where the key would arrive missed a case. A Shortcut is
    // window-level and fires no matter what has focus - the same reason Ctrl+S
    // has always worked reliably here.
    Shortcut {
        sequences: [StandardKey.Cancel]   // Escape
        enabled: root.isOpen
        onActivated: root.close()
    }

    function close() {
        isOpen = false
        filePath = ""
        fileData = ({})
        root.closed()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {} // Block underlying clicks
    }

    Rectangle {
        width: Math.min(parent.width - 60, 960)
        height: Math.min(parent.height - 60, 680)
        anchors.centerIn: parent
        radius: Theme.radiusLarge
        color: Theme.bgDialog
        border.color: Theme.borderActive
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            // Header
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: root.isEditMode ? "✏️ Edit File" : "👁 Quick View"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTitle
                    font.bold: true
                    color: Theme.textPrimary
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: root.fileData.fileName || ""
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeMedium
                    color: Theme.accent
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                    Layout.alignment: Qt.AlignVCenter
                    elide: Text.ElideMiddle
                }

                // File metadata badge
                Rectangle {
                    Layout.preferredHeight: 24
                    Layout.preferredWidth: metaText.implicitWidth + 16
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.radiusSmall
                    color: Theme.bgHeader
                    border.color: Theme.borderSubtle
                    border.width: 1
                    visible: metaText.text.length > 0
                    clip: true

                    Text {
                        id: metaText
                        anchors.centerIn: parent
                        text: {
                            let parts = []
                            if (root.fileData.formattedSize) parts.push(root.fileData.formattedSize)
                            if (root.fileData.mimeType) parts.push(root.fileData.mimeType)
                            return parts.join(" • ")
                        }
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: Theme.textSecondary
                    }
                }

                // Open in default app
                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.radiusSmall
                    color: extMouse.containsMouse ? Theme.bgHover : Theme.bgHeader
                    border.color: Theme.borderSubtle
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "↗"
                        font.pixelSize: 13
                        color: Theme.textPrimary
                    }

                    MouseArea {
                        id: extMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.previewService.openInDefaultApp(root.filePath)
                        ToolTip.visible: containsMouse
                        ToolTip.text: "Open with system default application"
                    }
                }

                // Close button
                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.radiusSmall
                    color: closeMouse.containsMouse ? Theme.dangerHover : Theme.bgHeader
                    border.color: Theme.borderSubtle
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        font.pixelSize: 12
                        color: closeMouse.containsMouse ? Theme.textSelected : Theme.textPrimary
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                        ToolTip.visible: containsMouse
                        ToolTip.text: "Close (Esc)"
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.borderSubtle
            }

            // Main Content Area
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // 1. Text Viewer / Editor
                ScrollView {
                    id: editorScroll
                    anchors.fill: parent
                    visible: !!(root.fileData && root.fileData.isText)
                    clip: true

                    // Configures the bar ScrollView already owns rather than
                    // assigning a replacement: a replacement is not the one the
                    // control lays out, so it ended up 12 pixels wide and 4
                    // high while the real bar stayed hidden.
                    //
                    // Always on, so a file that continues past the bottom of the
                    // window says so instead of looking like it ends there.
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOn
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical.contentItem: Rectangle {
                        implicitWidth: 8
                        radius: 4
                        color: Theme.borderSubtle
                    }

                    TextArea {
                        id: editorText
                        objectName: "editorText"
                        readOnly: !root.isEditMode || Boolean(root.fileData?.isTruncated)
                        selectByMouse: true
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeBase
                        color: Theme.textPrimary
                        background: Rectangle {
                            color: Theme.bgInput
                            border.color: Theme.borderSubtle
                            border.width: 1
                            radius: Theme.radiusSmall
                        }
                        wrapMode: TextArea.WrapAnywhere

                        /// Moves the caret a screenful at a time. A TextArea
                        /// does not implement Page Up or Page Down itself, and
                        /// the ScrollView around it only reacts to the caret
                        /// moving - so without this the keys did nothing at all
                        /// in a file too long to fit.
                        function pageBy(direction) {
                            const lineHeight = Math.max(1, editorText.cursorRectangle.height)
                            const visibleLines = Math.max(1, Math.floor(editorScroll.availableHeight / lineHeight) - 1)
                            const target = Qt.point(editorText.cursorRectangle.x,
                                                    editorText.cursorRectangle.y
                                                    + direction * visibleLines * lineHeight)
                            const at = editorText.positionAt(target.x, target.y)
                            editorText.cursorPosition =
                                (at >= 0) ? at
                                          : (direction > 0 ? editorText.length : 0)
                        }

                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_PageDown) {
                                editorText.pageBy(1)
                                event.accepted = true
                            } else if (event.key === Qt.Key_PageUp) {
                                editorText.pageBy(-1)
                                event.accepted = true
                            }
                        }

                        // Escape is handled here rather than relying on the key
                        // bubbling out through ScrollView to the root. Ctrl+S
                        // ends by focusing this editor, and once focus was in
                        // here Escape stopped reaching the root's handler.
                        Keys.onEscapePressed: (event) => {
                            root.close()
                            event.accepted = true
                        }

                        // The editor sees Tab before the dialog does, and Qt's
                        // own handling would move focus out of the modal and
                        // into the file panels behind it.
                        Keys.onTabPressed: (event) => { root.stepFocus(1); event.accepted = true; }
                        Keys.onBacktabPressed: (event) => { root.stepFocus(-1); event.accepted = true; }
                    }
                }

                // 2. Image Viewer
                Item {
                    anchors.fill: parent
                    visible: !!(root.fileData && root.fileData.isImage)

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        Image {
                            id: previewImage
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            source: root.fileData.fileUrl || ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: (root.fileData.imageWidth > 0 ? "Resolution: " + root.fileData.imageWidth + " x " + root.fileData.imageHeight + " px" : "")
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                        }
                    }
                }

                // 3. Binary / Other File View
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: Boolean(!root.fileData?.isText && !root.fileData?.isImage)
                    spacing: 12

                    Text {
                        text: root.fileData.error ? "🔒" : "📦"
                        font.pixelSize: 48
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: root.fileData.error ? "Cannot Read File" : "Binary File"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: root.fileData.error ? Theme.textDanger : Theme.textPrimary
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: root.fileData.error || "Binary or unsupported preview format."
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                        horizontalAlignment: Text.AlignHCenter
                        Layout.maximumWidth: 420
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 160
                        height: 36
                        radius: Theme.radiusSmall
                        color: openExtMouse.containsMouse ? Theme.accentHover : Theme.accent

                        Text {
                            anchors.centerIn: parent
                            text: "Open with System App"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: true
                            color: "#0f172a"
                        }

                        MouseArea {
                            id: openExtMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.previewService.openInDefaultApp(root.filePath)
                                root.close()
                            }
                        }
                    }
                }
            }

            // Footer
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: root.filePath
                    font.family: Theme.fontMono
                    font.pixelSize: 11
                    color: Theme.textMuted
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                // Only part of a large file is loaded, so saving would discard the rest.
                Text {
                    visible: Boolean(root.isEditMode && root.fileData?.isTruncated)
                    text: "⚠ Only the first part of this file is loaded — read-only"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.warning
                    elide: Text.ElideRight
                }

                Text {
                    visible: root.saveError.length > 0
                    text: root.saveError
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textDanger
                    elide: Text.ElideRight
                }

                Text {
                    visible: root.saveNotice.length > 0
                    text: "✓ " + root.saveNotice
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    color: Theme.success
                    elide: Text.ElideRight
                }

                // Save button if edit mode
                Rectangle {
                    id: saveButton
                    objectName: "saveButton"
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 30
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.radiusSmall
                    visible: Boolean(root.isEditMode && root.fileData?.isText && !root.fileData?.isTruncated)
                    color: saveMouse.containsMouse ? Theme.accentHover : Theme.accent
                    border.color: Theme.textPrimary
                    border.width: saveButton.activeFocus ? 2 : 0

                    // Qt's Tab handling runs on the focused item before the
                    // event reaches the dialog, so every item in the ring has
                    // to claim Tab itself or focus walks out of the modal.
                    Keys.onTabPressed: (event) => { root.stepFocus(1); event.accepted = true; }
                    Keys.onBacktabPressed: (event) => { root.stepFocus(-1); event.accepted = true; }
                    Keys.onSpacePressed: (event) => { root.activateFocused(); event.accepted = true; }
                    Keys.onReturnPressed: (event) => { root.activateFocused(); event.accepted = true; }
                    Keys.onEnterPressed: (event) => { root.activateFocused(); event.accepted = true; }

                    Text {
                        anchors.centerIn: parent
                        text: "💾 Save (Ctrl+S)"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: "#0f172a"
                    }

                    MouseArea {
                        id: saveMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.save(true)
                    }
                }

                // Close button
                Rectangle {
                    id: closeButton
                    objectName: "closeButton"
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 30
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.radiusSmall
                    color: dlgCloseMouse.containsMouse ? Theme.bgHover : Theme.bgHeader
                    border.color: closeButton.activeFocus ? Theme.textPrimary : Theme.borderSubtle
                    border.width: closeButton.activeFocus ? 2 : 1

                    // Qt's Tab handling runs on the focused item before the
                    // event reaches the dialog, so every item in the ring has
                    // to claim Tab itself or focus walks out of the modal.
                    Keys.onTabPressed: (event) => { root.stepFocus(1); event.accepted = true; }
                    Keys.onBacktabPressed: (event) => { root.stepFocus(-1); event.accepted = true; }
                    Keys.onSpacePressed: (event) => { root.activateFocused(); event.accepted = true; }
                    Keys.onReturnPressed: (event) => { root.activateFocused(); event.accepted = true; }
                    Keys.onEnterPressed: (event) => { root.activateFocused(); event.accepted = true; }

                    Text {
                        anchors.centerIn: parent
                        text: "Close (Esc)"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textPrimary
                    }

                    MouseArea {
                        id: dlgCloseMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }
        }
    }

    // Consume anything the editor did not take, so keys cannot reach the file
    // list behind this dialog. The TextArea handles Enter itself in edit mode,
    // so newlines still work there.
    Keys.onPressed: (event) => {
        // Tab is handled here rather than in a Keys.onTabPressed because this
        // item has a blanket accept at the end. Specific handlers do run first
        // - Keys.onPressed only sees what they leave - but there was no Tab
        // handler on this item at all, so Tab fell through to the accept below
        // and vanished. In view mode the root holds focus, so that was the only
        // place Tab ever arrived. Either shape works; this keeps the routing in
        // one place, next to the accept that would otherwise hide it.
        if (event.key === Qt.Key_Tab) {
            root.stepFocus(1)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Backtab) {
            root.stepFocus(-1)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Escape) {
            root.close()
        }
        // Nothing else travels on to the panels behind a modal.
        event.accepted = true
    }
}
