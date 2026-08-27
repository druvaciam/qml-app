import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

Rectangle {
    id: root
    required property PanelController controller

    height: Theme.driveBarHeight
    color: Theme.bgHeader
    border.color: Theme.borderSubtle
    border.width: 1
    radius: Theme.radiusSmall

    RowLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 6

        // Drive buttons list
        Row {
            Layout.fillHeight: true
            spacing: 4

            Repeater {
                model: root.controller.driveList

                Rectangle {
                    required property driveInfo modelData
                    
                    readonly property bool isCurrentDrive: {
                        let path = root.controller.currentPath.toUpperCase()
                        let dPath = modelData.rootPath.toUpperCase()
                        return path.startsWith(dPath)
                    }

                    width: driveBtnContent.width + 16
                    height: parent.height
                    radius: Theme.radiusSmall
                    color: isCurrentDrive ? Theme.bgSelected : (driveMouse.containsMouse ? Theme.bgHover : "transparent")
                    border.color: isCurrentDrive ? Theme.accent : (driveMouse.containsMouse ? Theme.borderSubtle : "transparent")
                    border.width: 1

                    Row {
                        id: driveBtnContent
                        anchors.centerIn: parent
                        spacing: 4

                        Text {
                            text: "💾 " + (modelData.rootPath.length > 3 ? modelData.rootPath.substring(0, 3) : modelData.rootPath)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: isCurrentDrive
                            color: isCurrentDrive ? Theme.textSelected : Theme.textPrimary
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: modelData.formattedFree + " free"
                            font.family: Theme.fontFamily
                            font.pixelSize: 10
                            color: isCurrentDrive ? "#bae6fd" : Theme.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                            visible: modelData.formattedFree.length > 0
                        }
                    }

                    MouseArea {
                        id: driveMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.controller.changeDrive(modelData.rootPath)
                        }

                        ToolTip.visible: containsMouse
                        ToolTip.text: (modelData.displayName.length > 0 ? modelData.displayName + "\n" : "") +
                                      "Total: " + modelData.formattedTotal + "\n" +
                                      "Free: " + modelData.formattedFree + " (" + Math.round((1.0 - modelData.percentUsed) * 100) + "%)"
                        ToolTip.delay: 400
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // Refresh Drives Button
        Rectangle {
            width: 26
            height: 26
            radius: Theme.radiusSmall
            color: refreshMouse.containsMouse ? Theme.bgHover : "transparent"
            border.color: refreshMouse.containsMouse ? Theme.borderSubtle : "transparent"

            Text {
                anchors.centerIn: parent
                text: "🔄"
                font.pixelSize: 12
            }

            MouseArea {
                id: refreshMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.controller.refreshDrives()
                ToolTip.visible: containsMouse
                ToolTip.text: "Refresh drive list"
                ToolTip.delay: 300
            }
        }
    }
}
