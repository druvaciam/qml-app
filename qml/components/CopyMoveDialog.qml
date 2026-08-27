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
    focus: true

    function open(items, targetDir, move = false) {
        itemsList = items || []
        destinationPath = targetDir || ""
        isMove = move
        targetInput.text = targetDir || ""
        isOpen = true
        root.focus = true
        root.forceActiveFocus()
        targetInput.forceActiveFocus()
        targetInput.selectAll()
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
                Layout.fillWidth: true
                height: 34
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textPrimary
                focus: true
                selectByMouse: true
                background: Rectangle {
                    color: Theme.bgInput
                    border.color: targetInput.activeFocus ? Theme.accent : Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radiusSmall
                }
                onAccepted: root.confirm()
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
                    width: 95
                    height: 32
                    radius: Theme.radiusSmall
                    color: cancelMouse.containsMouse ? Theme.bgHover : Theme.bgHeader
                    border.color: Theme.borderSubtle
                    border.width: 1

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
                    width: 125
                    height: 32
                    radius: Theme.radiusSmall
                    color: okMouse.containsMouse ? Theme.accentHover : Theme.accent

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
    Keys.onReturnPressed: (event) => { root.confirm(); event.accepted = true; }
    Keys.onEnterPressed: (event) => { root.confirm(); event.accepted = true; }
}
