import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

FocusScope {
    id: root

    signal accepted(var items, string destination, bool isMove)
    signal rejected()
    signal closed()

    property var itemsList: []
    property string destinationPath: ""
    property bool isMove: false
    property bool isOpen: false

    visible: isOpen
    anchors.fill: parent
    z: 150
    objectName: "copyMoveDialog"
    focus: true

    /// What Tab cycles through. Qt's own focus chain walks the entire scene,
    /// and the panels behind a dialog are still visible items - so Tab used to
    /// step straight out of the dialog and into the file list, which is not
    /// what "modal" means to anyone.
    readonly property var focusRing: [targetInput, okButton, cancelButton]

    function stepFocus(delta) {
        let at = -1
        for (let i = 0; i < root.focusRing.length; ++i) {
            if (root.focusRing[i].activeFocus) {
                at = i
                break
            }
        }
        // Nothing in the ring has it yet - the dialog itself does - so Tab
        // starts at the beginning and Shift+Tab at the end.
        const next = (at < 0) ? (delta > 0 ? 0 : root.focusRing.length - 1)
                              : (at + delta + root.focusRing.length) % root.focusRing.length
        root.focusRing[next].forceActiveFocus()
    }

    function open(items, targetDir, move = false) {
        itemsList = items || []
        destinationPath = targetDir || ""
        isMove = move
        targetInput.text = targetDir || ""
        isOpen = true
        root.focus = true
        // Focus stays on the dialog itself rather than the path field. The
        // path is nearly always right as offered, so the useful default is
        // "press Enter to copy" - not "the whole path is selected and one
        // keystroke replaces it". Click the field to edit it.
        root.forceActiveFocus()
    }

    function close() {
        isOpen = false
        root.rejected()
        root.closed()
    }

    function confirm() {
        let dest = targetInput.text.trim()
        if (!dest || dest.length === 0) {
            dest = root.destinationPath
        }
        if (!dest || dest.length === 0) {
            return
        }
        isOpen = false
        root.accepted(root.itemsList, dest, root.isMove)
        root.closed()
    }

    // Modal dim backdrop
    Rectangle {
        anchors.fill: parent
        color: "#a0000000"
        z: 0

        MouseArea {
            anchors.fill: parent
            onClicked: {} // Block click-through to panels
        }
    }

    // Dialog card
    Rectangle {
        id: dialogCard
        width: 520
        height: 310
        anchors.centerIn: parent
        radius: Theme.radiusLarge
        color: Theme.bgDialog
        border.color: Theme.accent
        border.width: 1
        z: 10
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            // Title row
            RowLayout {
                spacing: 8
                Text {
                    text: root.isMove ? "🚚" : "📋"
                    font.pixelSize: 22
                }

                Text {
                    text: root.isMove ? "Move / Rename (F6)" : "Copy (F5)"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                    color: Theme.accent
                }
            }

            // Prompt
            Text {
                text: (root.isMove ? "Move " : "Copy ") + root.itemsList.length + " item(s) to:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                color: Theme.textPrimary
            }

            // Target directory text field
            TextField {
                id: targetInput
                objectName: "targetInput"
                Layout.fillWidth: true
                height: 34
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textPrimary
                selectByMouse: true
                background: Rectangle {
                    color: Theme.bgInput
                    border.color: targetInput.activeFocus ? Theme.accent : Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radiusSmall
                }
                onAccepted: root.confirm()
                // Handled here as well as on the dialog: a TextField sees the
                // key first, and Qt's default handling would move focus out of
                // the dialog before the event ever reached the root.
                Keys.onTabPressed: (event) => { root.stepFocus(1); event.accepted = true; }
                Keys.onBacktabPressed: (event) => { root.stepFocus(-1); event.accepted = true; }
                Keys.onReturnPressed: (event) => { root.confirm(); event.accepted = true; }
                Keys.onEnterPressed: (event) => { root.confirm(); event.accepted = true; }
                Keys.onEscapePressed: (event) => { root.close(); event.accepted = true; }
            }

            // Scrollable list of items to be copied/moved
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusSmall
                color: Theme.bgInput
                border.color: Theme.borderSubtle
                border.width: 1

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true

                    ListView {
                        model: root.itemsList
                        delegate: Text {
                            text: "• " + modelData
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                            elide: Text.ElideMiddle
                            width: parent.width
                        }
                    }
                }
            }

            // Bottom Buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.fillWidth: true }

                // Cancel Button
                Rectangle {
                    id: cancelButton
                    objectName: "cancelButton"
                    activeFocusOnTab: true

                    // Qt's own Tab handling runs on the focused item before the
                    // event ever reaches the dialog root, so every item in the
                    // ring has to claim Tab for itself or focus walks out.
                    Keys.onTabPressed: (event) => { root.stepFocus(1); event.accepted = true; }
                    Keys.onBacktabPressed: (event) => { root.stepFocus(-1); event.accepted = true; }
                    width: 95
                    height: 32
                    radius: Theme.radiusSmall
                    color: cancelMouse.containsMouse ? Theme.bgHover : Theme.bgHeader
                    border.color: cancelButton.activeFocus ? Theme.textPrimary : Theme.borderSubtle
                    border.width: cancelButton.activeFocus ? 2 : 1

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel (Esc)"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textPrimary
                    }

                    MouseArea {
                        id: cancelMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }

                // OK Button
                Rectangle {
                    id: okButton
                    objectName: "okButton"
                    activeFocusOnTab: true

                    // Qt's own Tab handling runs on the focused item before the
                    // event ever reaches the dialog root, so every item in the
                    // ring has to claim Tab for itself or focus walks out.
                    Keys.onTabPressed: (event) => { root.stepFocus(1); event.accepted = true; }
                    Keys.onBacktabPressed: (event) => { root.stepFocus(-1); event.accepted = true; }
                    width: 125
                    height: 32
                    radius: Theme.radiusSmall
                    color: okMouse.containsMouse ? Theme.accentHover : Theme.accent
                    // Says which button Enter will press: the dialog's default
                    // action, or whichever button Tab has landed on.
                    border.color: Theme.textPrimary
                    border.width: (okButton.activeFocus
                                   || (!targetInput.activeFocus && !cancelButton.activeFocus)) ? 2 : 0

                    Text {
                        anchors.centerIn: parent
                        text: root.isMove ? "🚚 Move (Enter)" : "📋 Copy (Enter)"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: "#0f172a"
                    }

                    MouseArea {
                        id: okMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.confirm()
                    }
                }
            }
        }
    }

    Keys.onEscapePressed: (event) => { root.close(); event.accepted = true; }
    Keys.onReturnPressed: (event) => { root.activateFocused(); event.accepted = true; }
    Keys.onEnterPressed: (event) => { root.activateFocused(); event.accepted = true; }
    Keys.onTabPressed: (event) => { root.stepFocus(1); event.accepted = true; }
    Keys.onBacktabPressed: (event) => { root.stepFocus(-1); event.accepted = true; }

    /// Enter presses whichever button has focus, and Copy when none does.
    /// Space does the same, which is what a focused button is expected to do.
    function activateFocused() {
        if (cancelButton.activeFocus) {
            root.close()
        } else {
            root.confirm()
        }
    }

    Keys.onSpacePressed: (event) => {
        if (okButton.activeFocus || cancelButton.activeFocus) {
            root.activateFocused()
            event.accepted = true
        }
    }

    // Nothing behind a modal should be reachable by keyboard. Anything not
    // handled above is swallowed here rather than allowed to travel on to the
    // panels underneath.
    Keys.onPressed: (event) => { event.accepted = true; }
}
