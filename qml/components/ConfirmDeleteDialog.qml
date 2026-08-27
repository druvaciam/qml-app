import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

Rectangle {
    id: root

    signal accepted(var items, bool permanent)
    signal rejected()
    signal closed()

    property var itemsList: []
    property bool isPermanent: false
    property bool isOpen: false

    visible: isOpen
    anchors.fill: parent
    color: "#a0000000"
    z: 110

    function open(items, permanent = false) {
        itemsList = items
        isPermanent = permanent
        isOpen = true
        root.forceActiveFocus()
    }

    function close() {
        isOpen = false
        root.rejected()
        root.closed()
    }

    function confirm() {
        isOpen = false
        root.accepted(root.itemsList, root.isPermanent)
        root.closed()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {}
    }

    Rectangle {
        width: 500
        height: 250
        anchors.centerIn: parent
        radius: Theme.radiusLarge
        color: Theme.bgDialog
        border.color: root.isPermanent ? Theme.danger : Theme.accent
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            RowLayout {
                spacing: 8
                Text {
                    text: root.isPermanent ? "⚠️" : "♻️"
                    font.pixelSize: 22
                }

                Text {
                    text: root.isPermanent ? "Permanently Delete (Shift+Del)" : "Move to Recycle Bin (Del / F8)"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                    color: root.isPermanent ? Theme.textDanger : Theme.accent
                }
            }

            Text {
                text: root.isPermanent ?
                      ("Are you sure you want to PERMANENTLY delete " + root.itemsList.length + " item(s)?\nThis cannot be undone.") :
                      ("Are you sure you want to move " + root.itemsList.length + " item(s) to the Recycle Bin?")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                color: Theme.textPrimary
            }

            // Scrollable list of items to be deleted
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

                // Delete / Trash Button
                Rectangle {
                    width: root.isPermanent ? 145 : 130
                    height: 30
                    radius: Theme.radiusSmall
                    color: root.isPermanent ?
                           (delMouse.containsMouse ? Theme.dangerHover : Theme.danger) :
                           (delMouse.containsMouse ? Theme.accentHover : Theme.accent)

                    Text {
                        anchors.centerIn: parent
                        text: root.isPermanent ? "🗑 Delete Forever" : "♻️ Move to Trash"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: root.isPermanent ? Theme.textSelected : "#0f172a"
                    }

                    MouseArea {
                        id: delMouse
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
    Keys.onReturnPressed: root.confirm()
    Keys.onEnterPressed: root.confirm()
}
