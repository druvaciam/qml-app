import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

Rectangle {
    id: root
    required property PanelController controller

    height: Theme.headerHeight
    color: Theme.bgHeader
    border.color: Theme.borderSubtle
    border.width: 1
    radius: Theme.radiusSmall

    property bool isEditing: false

    RowLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        // Back button
        Rectangle {
            width: 26
            height: 26
            radius: Theme.radiusSmall
            color: root.controller.canGoBack ? (backMouse.containsMouse ? Theme.bgHover : "transparent") : "transparent"
            opacity: root.controller.canGoBack ? 1.0 : 0.4

            Text {
                anchors.centerIn: parent
                text: "◀"
                font.pixelSize: 11
                color: root.controller.canGoBack ? Theme.textPrimary : Theme.textMuted
            }

            MouseArea {
                id: backMouse
                anchors.fill: parent
                enabled: root.controller.canGoBack
                hoverEnabled: true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.controller.goBack()
                ToolTip.visible: containsMouse && enabled
                ToolTip.text: "Back (Alt+Left)"
            }
        }

        // Forward button
        Rectangle {
            width: 26
            height: 26
            radius: Theme.radiusSmall
            color: root.controller.canGoForward ? (fwdMouse.containsMouse ? Theme.bgHover : "transparent") : "transparent"
            opacity: root.controller.canGoForward ? 1.0 : 0.4

            Text {
                anchors.centerIn: parent
                text: "▶"
                font.pixelSize: 11
                color: root.controller.canGoForward ? Theme.textPrimary : Theme.textMuted
            }

            MouseArea {
                id: fwdMouse
                anchors.fill: parent
                enabled: root.controller.canGoForward
                hoverEnabled: true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.controller.goForward()
                ToolTip.visible: containsMouse && enabled
                ToolTip.text: "Forward (Alt+Right)"
            }
        }

        // Up directory button (..)
        Rectangle {
            width: 26
            height: 26
            radius: Theme.radiusSmall
            color: upMouse.containsMouse ? Theme.bgHover : "transparent"

            Text {
                anchors.centerIn: parent
                text: "▲"
                font.pixelSize: 11
                color: Theme.textPrimary
            }

            MouseArea {
                id: upMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.controller.navigateUp()
                ToolTip.visible: containsMouse
                ToolTip.text: "Parent Directory (Backspace / ..)"
            }
        }

        // Root directory button (\)
        Rectangle {
            width: 26
            height: 26
            radius: Theme.radiusSmall
            color: rootMouse.containsMouse ? Theme.bgHover : "transparent"

            Text {
                anchors.centerIn: parent
                text: "📁"
                font.pixelSize: 11
                color: Theme.textPrimary
            }

            MouseArea {
                id: rootMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.controller.navigateRoot()
                ToolTip.visible: containsMouse
                ToolTip.text: "Drive Root Directory (\)"
            }
        }

        // Path Input / Display Box
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: root.isEditing ? Theme.bgInput : Theme.bgPanel
            border.color: root.isEditing ? Theme.accent : Theme.borderSubtle
            border.width: 1

            // Path Text Field when editing
            TextField {
                id: pathInput
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                visible: root.isEditing
                text: root.controller.currentPath
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeBase
                color: Theme.textPrimary
                verticalAlignment: TextInput.AlignVCenter
                background: null
                selectByMouse: true

                onAccepted: {
                    root.controller.navigateTo(text)
                    root.isEditing = false
                }

                Keys.onEscapePressed: {
                    text = root.controller.currentPath
                    root.isEditing = false
                }

                onActiveFocusChanged: {
                    if (!activeFocus) {
                        root.isEditing = false
                    }
                }
            }

            // Path Display (clickable to start editing or click breadcrumbs)
            Item {
                id: pathDisplay
                anchors.fill: parent
                visible: !root.isEditing

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: Text.AlignVCenter
                    text: root.controller.currentPath
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeBase
                    color: Theme.textPrimary
                    elide: Text.ElideMiddle
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.IBeamCursor
                    onClicked: {
                        root.isEditing = true
                        pathInput.text = root.controller.currentPath
                        pathInput.forceActiveFocus()
                        pathInput.selectAll()
                    }
                }
            }
        }

        // Copy Path Button
        Rectangle {
            width: 26
            height: 26
            radius: Theme.radiusSmall
            color: copyMouse.containsMouse ? Theme.bgHover : "transparent"

            Text {
                anchors.centerIn: parent
                text: "📋"
                font.pixelSize: 12
            }

            MouseArea {
                id: copyMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    // Put current path on clipboard
                    pathInput.text = root.controller.currentPath
                    pathInput.selectAll()
                    pathInput.copy()
                }
                ToolTip.visible: containsMouse
                ToolTip.text: "Copy path to clipboard"
            }
        }
    }
}
