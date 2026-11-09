import QtQuick
import QtQuick.Controls
import QmlCommander

// One button per tab, like the drive bar, each with its own close button.
// Shown only when the panel has more than one tab.
Rectangle {
    id: root
    required property PanelController controller

    /// A tab was clicked; the panel gives the keyboard back to its list.
    signal tabClicked()

    height: Theme.driveBarHeight
    color: Theme.bgHeader
    border.color: Theme.borderSubtle
    border.width: 1
    radius: Theme.radiusSmall

    function tabTitle(path) {
        const trimmed = path.length > 1 && path.endsWith("/") ? path.slice(0, -1) : path
        const name = trimmed.slice(trimmed.lastIndexOf("/") + 1)
        return name.length > 0 ? name : trimmed
    }

    ListView {
        id: tabList
        objectName: "tabList"
        anchors.fill: parent
        anchors.margins: 4
        orientation: ListView.Horizontal
        spacing: 4
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.controller.tabPaths
        currentIndex: root.controller.currentTab
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

        delegate: Rectangle {
            id: tab
            required property string modelData
            required property int index
            readonly property bool isCurrentTab: index === root.controller.currentTab

            width: Math.min(tabContent.implicitWidth + 16, 220)
            height: ListView.view.height
            radius: Theme.radiusSmall
            color: isCurrentTab ? Theme.bgSelected : (tabMouse.containsMouse ? Theme.bgHover : "transparent")
            border.color: isCurrentTab ? Theme.accent : (tabMouse.containsMouse ? Theme.borderSubtle : "transparent")
            border.width: 1

            MouseArea {
                id: tabMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                // Closing destroys this button, so nothing may run after it.
                onClicked: (mouse) => {
                    root.controller.activate()
                    root.tabClicked()
                    if (mouse.button === Qt.MiddleButton) {
                        root.controller.closeTab(tab.index)
                    } else {
                        root.controller.switchTab(tab.index)
                    }
                }

                ToolTip.visible: containsMouse && !closeMouse.containsMouse
                ToolTip.text: tab.modelData
                ToolTip.delay: 400
            }

            Row {
                id: tabContent
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                Text {
                    width: Math.min(implicitWidth, 220 - 16 - 6 - closeButton.width)
                    text: "📁 " + root.tabTitle(tab.modelData)
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: tab.isCurrentTab
                    color: tab.isCurrentTab ? Theme.textSelected : Theme.textPrimary
                    anchors.verticalCenter: parent.verticalCenter
                }

                Rectangle {
                    id: closeButton
                    width: 16
                    height: 16
                    radius: 8
                    anchors.verticalCenter: parent.verticalCenter
                    color: closeMouse.containsMouse ? Theme.bgHover : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        font.pixelSize: 10
                        color: closeMouse.containsMouse ? Theme.textPrimary : Theme.textMuted
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.controller.activate()
                            root.tabClicked()
                            root.controller.closeTab(tab.index)
                        }

                        ToolTip.visible: containsMouse
                        ToolTip.text: qsTr("Close tab (Ctrl+W)")
                        ToolTip.delay: 400
                    }
                }
            }
        }
    }
}
