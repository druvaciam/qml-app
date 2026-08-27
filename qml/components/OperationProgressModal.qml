import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

Rectangle {
    id: root
    required property FileOperationsService fileOps

    visible: fileOps.isBusy
    anchors.fill: parent
    color: "#a0000000" // Dim backdrop
    z: 100

    // Prevent clicks from passing through
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
    }

    Rectangle {
        width: 460
        height: 190
        anchors.centerIn: parent
        radius: Theme.radiusLarge
        color: Theme.bgDialog
        border.color: Theme.accent
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            // Title
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "⏳"
                    font.pixelSize: 18
                }

                Text {
                    text: root.fileOps.operationTitle
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                    color: Theme.textPrimary
                    Layout.fillWidth: true
                }
            }

            // Current file name
            Text {
                Layout.fillWidth: true
                text: root.fileOps.currentFileName.length > 0 ? "Processing: " + root.fileOps.currentFileName : root.fileOps.statusMessage
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                elide: Text.ElideMiddle
            }

            // Progress Bar
            Rectangle {
                Layout.fillWidth: true
                height: 12
                radius: 6
                color: Theme.bgInput
                border.color: Theme.borderSubtle
                border.width: 1
                clip: true

                Rectangle {
                    height: parent.height
                    width: parent.width * Math.max(0.02, Math.min(1.0, root.fileOps.progress))
                    radius: 6
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.accent }
                        GradientStop { position: 1.0; color: Theme.accentHover }
                    }

                    Behavior on width {
                        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
                    }
                }
            }

            // Stats row
            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: root.fileOps.totalItems > 0 ?
                          root.fileOps.processedItems + " of " + root.fileOps.totalItems + " items" :
                          "Scanning files..."
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textMuted
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: Math.round(root.fileOps.progress * 100) + "%"
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    color: Theme.accent
                }
            }

            // Cancel Button
            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 90
                    height: 28
                    radius: Theme.radiusSmall
                    color: cancelMouse.containsMouse ? Theme.dangerHover : Theme.bgHeader
                    border.color: Theme.borderSubtle
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: cancelMouse.containsMouse ? Theme.textSelected : Theme.textPrimary
                    }

                    MouseArea {
                        id: cancelMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.fileOps.cancel()
                    }
                }
            }
        }
    }
}
