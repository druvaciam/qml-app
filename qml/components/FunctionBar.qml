import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

Rectangle {
    id: root

    signal requestView()
    signal requestEdit()
    signal requestCopy()
    signal requestMove()
    signal requestNewFolder()
    signal requestDelete()
    signal requestSwap()
    signal requestTerminal()
    signal requestRefresh()

    height: Theme.functionBarHeight
    color: Theme.bgHeader
    border.color: Theme.borderSubtle
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        // Function Key Button Component
        component FunctionButton: Rectangle {
            id: btn
            property string keyNumber: ""
            property string actionLabel: ""
            property color accentColor: Theme.accent
            signal clicked()

            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: btnMouse.pressed ? Theme.bgSelected : (btnMouse.containsMouse ? Theme.bgHover : Theme.bgPanel)
            border.color: btnMouse.containsMouse ? btn.accentColor : Theme.borderSubtle
            border.width: 1

            RowLayout {
                anchors.centerIn: parent
                spacing: 4

                // Key Badge
                Rectangle {
                    width: 22
                    height: 18
                    radius: 3
                    color: btnMouse.containsMouse ? btn.accentColor : Theme.bgHeader
                    border.color: Theme.borderSubtle
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: btn.keyNumber
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        font.bold: true
                        color: btnMouse.containsMouse ? "#0f172a" : Theme.textAccent
                    }
                }

                // Action Label
                Text {
                    text: btn.actionLabel
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    color: btnMouse.containsMouse ? Theme.textSelected : Theme.textPrimary
                }
            }

            MouseArea {
                id: btnMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: btn.clicked()
            }
        }

        FunctionButton {
            keyNumber: "F3"
            actionLabel: "View"
            onClicked: root.requestView()
        }

        FunctionButton {
            keyNumber: "F4"
            actionLabel: "Edit"
            onClicked: root.requestEdit()
        }

        FunctionButton {
            keyNumber: "F5"
            actionLabel: "Copy"
            accentColor: Theme.success
            onClicked: root.requestCopy()
        }

        FunctionButton {
            keyNumber: "F6"
            actionLabel: "Move"
            accentColor: Theme.warning
            onClicked: root.requestMove()
        }

        FunctionButton {
            keyNumber: "F7"
            actionLabel: "NewFolder"
            onClicked: root.requestNewFolder()
        }

        FunctionButton {
            keyNumber: "F8"
            actionLabel: "Delete"
            accentColor: Theme.danger
            onClicked: root.requestDelete()
        }

        // Auxiliary actions
        Rectangle {
            width: 32
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: swapMouse.containsMouse ? Theme.bgHover : Theme.bgPanel
            border.color: swapMouse.containsMouse ? Theme.accent : Theme.borderSubtle

            Text {
                anchors.centerIn: parent
                text: "⇄"
                font.pixelSize: 14
                color: Theme.textPrimary
            }

            MouseArea {
                id: swapMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.requestSwap()
                ToolTip.visible: containsMouse
                ToolTip.text: "Swap Panes (Ctrl+U)"
            }
        }

        Rectangle {
            width: 32
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: termMouse.containsMouse ? Theme.bgHover : Theme.bgPanel
            border.color: termMouse.containsMouse ? Theme.accent : Theme.borderSubtle

            Text {
                anchors.centerIn: parent
                text: "⌨"
                font.pixelSize: 14
                color: Theme.textPrimary
            }

            MouseArea {
                id: termMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.requestTerminal()
                ToolTip.visible: containsMouse
                ToolTip.text: "Open Terminal in active folder"
            }
        }

        Rectangle {
            width: 32
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: refrMouse.containsMouse ? Theme.bgHover : Theme.bgPanel
            border.color: refrMouse.containsMouse ? Theme.accent : Theme.borderSubtle

            Text {
                anchors.centerIn: parent
                text: "↻"
                font.pixelSize: 14
                color: Theme.textPrimary
            }

            MouseArea {
                id: refrMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.requestRefresh()
                ToolTip.visible: containsMouse
                ToolTip.text: "Refresh All (Ctrl+R)"
            }
        }
    }
}
