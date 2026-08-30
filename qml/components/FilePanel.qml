import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

// A FocusScope, not a plain Rectangle. Both panels contain a file list that
// declares `focus: true`; without a scope around each panel those two lists sit
// in the same focus scope and compete for it. Whenever focus was re-resolved -
// after a paste, which reloads both panels and destroys every delegate - the
// later-declared one won, so the keyboard silently moved to the other pane.
// A binding on `focus` cannot fix that, because forceActiveFocus() assigns to
// `focus` directly and breaks the binding on the first mouse press.
FocusScope {
    id: root
    required property PanelController controller
    property alias fileListView: fileListView
    property alias filterInput: filterInput
    property bool isDragTarget: false
    signal requestDelete(bool permanent)
    signal requestCopy()
    signal requestMove()
    signal requestDropCopy(var paths, string destination, bool isMove)

    // The visual frame moved in here when the root became a FocusScope.
    Rectangle {
        id: frame
        anchors.fill: parent
        color: root.controller.isActive ? Theme.bgPanelActive : Theme.bgPanel
        border.color: (root.isDragTarget || fileListView.containsDrag) ? Theme.accent :
                      (root.controller.isActive ? Theme.borderActive : Theme.borderSubtle)
        border.width: (root.isDragTarget || fileListView.containsDrag || root.controller.isActive) ? 2 : 1
        radius: Theme.radiusBase
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 4

            // Drive selection bar
            DriveSelector {
                Layout.fillWidth: true
                controller: root.controller
            }

            // Breadcrumb & Path address bar
            BreadcrumbBar {
                Layout.fillWidth: true
                controller: root.controller
            }

            // File List View
            FileListView {
                id: fileListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: root.controller
                onRequestDelete: (permanent) => root.requestDelete(permanent)
                onRequestCopy: root.requestCopy()
                onRequestMove: root.requestMove()
                onRequestDropCopy: (paths, destination, isMove) => root.requestDropCopy(paths, destination, isMove)
            }

            // Quick Filter Bar
            Rectangle {
                Layout.fillWidth: true
                height: 28
                color: filterInput.activeFocus ? Theme.bgInput : Theme.bgHeader
                border.color: filterInput.activeFocus ? Theme.accent : Theme.borderSubtle
                border.width: 1
                radius: Theme.radiusSmall

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 6

                    Text {
                        text: "🔍"
                        font.pixelSize: 11
                    }

                    TextField {
                        id: filterInput
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        placeholderText: "Filter files (*.cpp, name...)"
                        placeholderTextColor: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textPrimary
                        background: null
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true

                        // Deliberately NOT `text: root.controller.filterText`.
                        //
                        // That is a two-way binding: the field writes the
                        // controller, the controller writes back into the field.
                        // Re-assigning `text` on a TextInput resets its cursor and
                        // selection, so every character typed came back as an
                        // assignment that disturbed the field under the caret.
                        // The Connections block below pushes the value one way
                        // only, and never while it already matches.
                        Connections {
                            target: root.controller
                            function onFilterTextChanged(filter) {
                                if (filterInput.text !== filter) {
                                    filterInput.text = filter
                                }
                            }
                        }

                        // Applying the filter re-reads the whole directory. Doing
                        // that per keystroke means eight reloads to type eight
                        // characters, each one rebuilding every row.
                        Timer {
                            id: filterDebounce
                            interval: 250
                            repeat: false
                            onTriggered: root.controller.filterText = filterInput.text
                        }

                        onTextEdited: filterDebounce.restart()

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.controller.activate()
                            }
                        }

                        Keys.onEscapePressed: {
                            filterDebounce.stop()
                            // Typing breaks the binding on `text`, so clearing the
                            // controller alone would leave the old text on screen.
                            filterInput.text = ""
                            root.controller.filterText = ""
                            fileListView.setFocus()
                        }

                        Keys.onReturnPressed: {
                            // Don't wait out the debounce when the user is done.
                            filterDebounce.stop()
                            root.controller.filterText = filterInput.text
                            fileListView.setFocus()
                        }

                        Keys.onDownPressed: {
                            fileListView.setFocus()
                        }
                    }

                    // Clear filter button
                    Rectangle {
                        width: 18
                        height: 18
                        radius: 9
                        color: clearFilterMouse.containsMouse ? Theme.bgHover : "transparent"
                        visible: filterInput.text.length > 0

                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            font.pixelSize: 10
                            color: Theme.textSecondary
                        }

                        MouseArea {
                            id: clearFilterMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                filterDebounce.stop()
                                filterInput.text = ""
                                root.controller.filterText = ""
                                fileListView.setFocus()
                            }
                        }
                    }

                    // Hidden Files Toggle
                    Rectangle {
                        width: hiddenRow.implicitWidth + 14
                        height: 22
                        radius: Theme.radiusSmall
                        color: root.controller.model.showHidden ? Theme.accent : (hiddenMouse.containsMouse ? Theme.bgHover : "transparent")
                        border.color: root.controller.model.showHidden ? Theme.accent : Theme.borderSubtle
                        border.width: 1

                        Row {
                            id: hiddenRow
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                text: root.controller.model.showHidden ? "👁" : "👁‍🗨"
                                font.pixelSize: 11
                                color: root.controller.model.showHidden ? "#0f172a" : Theme.textSecondary
                            }

                            Text {
                                text: "Hidden"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSmall
                                font.bold: root.controller.model.showHidden
                                color: root.controller.model.showHidden ? "#0f172a" : Theme.textSecondary
                            }
                        }

                        MouseArea {
                            id: hiddenMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.controller.toggleShowHidden()
                            ToolTip.visible: containsMouse
                            ToolTip.text: root.controller.model.showHidden ? "Hide hidden/system files (Ctrl+H)" : "Show hidden/system files (Ctrl+H)"
                        }
                    }
                }
            }

            // Panel Status Bar
            Rectangle {
                Layout.fillWidth: true
                height: Theme.statusBarHeight
                color: Theme.bgHeader
                border.color: Theme.borderSubtle
                border.width: 1
                radius: Theme.radiusSmall

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    // Total items
                    Text {
                        text: root.controller.totalItemsCount + " item(s)"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                    }

                    // Separator
                    Text {
                        text: "•"
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }

                    // Selection status
                    Text {
                        text: root.controller.selectedItemsCount > 0 ?
                              root.controller.selectedItemsCount + " selected (" + root.controller.selectedSizeFormatted + ")" :
                              "None selected"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: root.controller.selectedItemsCount > 0
                        color: root.controller.selectedItemsCount > 0 ? Theme.accent : Theme.textMuted
                    }

                    Item { Layout.fillWidth: true }

                    // Drive free space
                    Text {
                        text: root.controller.currentDriveInfo.formattedFree.length > 0 ?
                              "Free: " + root.controller.currentDriveInfo.formattedFree : ""
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            z: -1
            onPressed: {
                root.controller.activate()
                fileListView.setFocus()
            }
        }
    }

    function focusFilter() {
        root.controller.activate()
        filterInput.forceActiveFocus()
        filterInput.selectAll()
    }
}
