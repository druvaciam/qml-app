import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

// Asked before a copy or move would replace files that are already at the
// destination. Only reached on platforms without a native shell prompt.
FocusScope {
    id: root

    signal resolved(int policy, var sourcePaths, string destination, bool isMove)
    signal closed()

    property var conflictNames: []
    property var sourcePaths: []
    property string destination: ""
    property bool isMove: false
    property bool isOpen: false

    visible: isOpen
    anchors.fill: parent
    z: 160
    focus: isOpen

    function open(names, sources, dest, move) {
        conflictNames = names || []
        sourcePaths = sources || []
        destination = dest || ""
        isMove = move
        isOpen = true
        forceActiveFocus()
    }

    function choose(policy) {
        isOpen = false
        root.resolved(policy, root.sourcePaths, root.destination, root.isMove)
        root.closed()
    }

    function cancel() {
        isOpen = false
        root.closed()
    }

    Rectangle {
        anchors.fill: parent
        color: "#a0000000"
        MouseArea {
            anchors.fill: parent
            onClicked: {} // block the panels underneath
        }
    }

    Rectangle {
        width: 560
        height: 360
        anchors.centerIn: parent
        radius: Theme.radiusLarge
        color: Theme.bgDialog
        border.color: Theme.warning
        border.width: 1
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                spacing: 8
                Text {
                    text: "⚠"
                    font.pixelSize: 22
                    color: Theme.warning
                }
                Text {
                    text: root.conflictNames.length === 1 ? "This file already exists"
                                                          : "These files already exist"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                    color: Theme.warning
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.destination
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textMuted
                elide: Text.ElideMiddle
            }

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
                        model: root.conflictNames
                        delegate: Text {
                            required property string modelData
                            text: "• " + modelData
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                            elide: Text.ElideMiddle
                            width: ListView.view.width
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: "Your choice applies to all " + root.conflictNames.length + " item(s)."
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textMuted
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                component ChoiceButton: Rectangle {
                    id: btn
                    property string label: ""
                    property color accentColor: Theme.accent
                    property bool filled: false
                    signal clicked()

                    Layout.preferredHeight: 34
                    Layout.preferredWidth: btnText.implicitWidth + 24
                    radius: Theme.radiusSmall
                    color: filled ? (btnMouse.containsMouse ? Theme.accentHover : accentColor)
                                  : (btnMouse.containsMouse ? Theme.bgHover : Theme.bgHeader)
                    border.color: filled ? accentColor : Theme.borderSubtle
                    border.width: 1

                    Text {
                        id: btnText
                        anchors.centerIn: parent
                        text: btn.label
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: btn.filled
                        color: btn.filled ? "#0f172a" : Theme.textPrimary
                    }

                    MouseArea {
                        id: btnMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: btn.clicked()
                    }
                }

                ChoiceButton {
                    label: "Cancel (Esc)"
                    onClicked: root.cancel()
                }

                Item { Layout.fillWidth: true }

                ChoiceButton {
                    label: "Skip these"
                    onClicked: root.choose(FileOperationsService.Skip)
                }

                ChoiceButton {
                    label: "Keep both"
                    onClicked: root.choose(FileOperationsService.KeepBoth)
                }

                ChoiceButton {
                    label: "Replace (Enter)"
                    filled: true
                    accentColor: Theme.warning
                    onClicked: root.choose(FileOperationsService.Overwrite)
                }
            }
        }
    }

    // Every key is consumed here. Without this, Enter fell straight through to
    // the file list behind the dialog and opened whatever the cursor was on,
    // leaving a preview window sitting under a modal that was still waiting for
    // an answer. A modal must not leak keys to what it is covering.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            // Replace is the accented default button, so Enter picks it.
            root.choose(FileOperationsService.Overwrite)
        } else if (event.key === Qt.Key_Escape) {
            root.cancel()
        }
        event.accepted = true
    }
}
