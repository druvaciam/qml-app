import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

Rectangle {
    id: root

    signal accepted(string folderName)
    signal rejected()
    signal closed()

    property bool isOpen: false

    visible: isOpen
    anchors.fill: parent
    color: "#a0000000"
    z: 110

    function open() {
        folderNameInput.text = "New Folder"
        isOpen = true
        folderNameInput.forceActiveFocus()
        folderNameInput.selectAll()
    }

    function close() {
        isOpen = false
        root.rejected()
        root.closed()
    }

    function confirm() {
        let name = folderNameInput.text.trim()
        if (name.length > 0) {
            isOpen = false
            root.accepted(name)
            root.closed()
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {}
    }

    Rectangle {
        width: 420
        height: 170
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
                text: "📁 Create New Folder (F7)"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.textPrimary
            }

            Rectangle {
                Layout.fillWidth: true
                height: 36
                radius: Theme.radiusSmall
                color: Theme.bgInput
                border.color: Theme.accent
                border.width: 1

                TextField {
                    id: folderNameInput
                    anchors.fill: parent
                    anchors.margins: 1
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    color: Theme.textPrimary
                    background: null
                    selectByMouse: true
                    verticalAlignment: TextInput.AlignVCenter
                    // A TextField carries its own padding on top of the anchor
                    // margins. With margins of 6 inside a 34px box that left only
                    // ~10px for 13px glyphs, so they were clipped top and bottom.
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 8
                    rightPadding: 8

                    onAccepted: root.confirm()
                    Keys.onEscapePressed: root.close()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }

                // Cancel Button
                Rectangle {
                    width: 80
                    height: 30
                    radius: Theme.radiusSmall
                    color: cancelMouse.containsMouse ? Theme.bgHover : Theme.bgHeader
                    border.color: Theme.borderSubtle
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
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

                // Create Button
                Rectangle {
                    width: 90
                    height: 30
                    radius: Theme.radiusSmall
                    color: okMouse.containsMouse ? Theme.accentHover : Theme.accent

                    Text {
                        anchors.centerIn: parent
                        text: "Create"
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

    Keys.onEscapePressed: root.close()
}
