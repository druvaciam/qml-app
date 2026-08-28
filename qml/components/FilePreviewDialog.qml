import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

Rectangle {
    id: root
    required property FilePreviewService previewService

    property string filePath: ""
    property var fileData: ({})
    property bool isOpen: false
    property bool isEditMode: false
    property string saveError: ""

    visible: isOpen
    anchors.fill: parent
    color: "#b0000000"
    z: 110

    signal closed()

    function open(path, editMode = false) {
        filePath = path
        isEditMode = editMode
        saveError = ""
        fileData = previewService.loadPreview(path)
        if (fileData.isText) {
            editorText.text = fileData.content || ""
        }
        isOpen = true
        root.forceActiveFocus()
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
                    anchors.fill: parent
                    visible: !!(root.fileData && root.fileData.isText)
                    clip: true

                    TextArea {
                        id: editorText
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

                // Save button if edit mode
                Rectangle {
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 30
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.radiusSmall
                    visible: Boolean(root.isEditMode && root.fileData?.isText && !root.fileData?.isTruncated)
                    color: saveMouse.containsMouse ? Theme.accentHover : Theme.accent

                    Text {
                        anchors.centerIn: parent
                        text: "💾 Save"
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
                        onClicked: {
                            if (root.previewService.saveTextFile(root.filePath, editorText.text)) {
                                root.close()
                            } else {
                                root.saveError = "Could not save — the file is read-only or in use. Your changes are still here."
                            }
                        }
                    }
                }

                // Close button
                Rectangle {
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 30
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.radiusSmall
                    color: dlgCloseMouse.containsMouse ? Theme.bgHover : Theme.bgHeader
                    border.color: Theme.borderSubtle
                    border.width: 1

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

    Keys.onEscapePressed: root.close()
}
