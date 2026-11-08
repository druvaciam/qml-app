import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QmlCommander

/**
 * A PDF, one page under the next.
 *
 * Every page is an Image fed by the "pdfpage" provider, which draws it at the
 * width it is shown at. Only pages near the view are asked for, so opening a
 * long document costs one page, not all of them.
 */
Item {
    id: root
    objectName: "pdfViewPanel"

    /// Set by the preview window; opening a new file reloads everything.
    property string filePath: ""
    // A page typed, or text selected, in one document means nothing in the next.
    onFilePathChanged: {
        pageField.clear()
        clearSelection()
    }

    /// How much of the available width one page takes. The buttons change it.
    property real zoom: 1.0
    readonly property real minZoom: 0.25
    readonly property real maxZoom: 4.0

    /// How far the arrow keys move the view: a nudge, the way a reader creeps
    /// down a page, rather than the near-screenful that Page Down gives.
    readonly property int lineStep: 60

    readonly property int pageCount: doc.pageCount

    /// Which page covers the middle of the view, for the bar along the bottom.
    /// Kept here rather than in the list's own current item: setting that makes
    /// the list keep the item in view, which snaps the page to the top of the
    /// view and pulls the view back to it when a page finishes loading.
    property int currentPage: 1

    /// A number is being typed into the bar, a page or a zoom. Escape then
    /// cancels the typing rather than closing the preview.
    readonly property bool typingInBar: pageField.activeFocus || zoomField.activeFocus

    /// Enter or Escape in one of the bar's fields: the keyboard goes back to
    /// whoever had it, so Page Down and the arrows work again straight away.
    signal barEntryFinished()

    // ---- selected text ----
    // Where the drag started and where it has got to, each a page and a
    // character on it. A drag can go backwards, so these are not in reading
    // order; selectionOn() puts them in it.
    property int anchorPage: -1
    property int anchorChar: -1
    property int focusPage: -1
    property int focusChar: -1
    readonly property bool hasSelection: anchorPage >= 0 && focusPage >= 0

    function select(fromPage, fromChar, toPage, toChar) {
        anchorPage = fromPage
        anchorChar = fromChar
        focusPage = toPage
        focusChar = toChar
    }

    function clearSelection() {
        select(-1, -1, -1, -1)
    }

    /// The characters selected on one page, as [from, to], or null.
    function selectionOn(page) {
        if (!hasSelection) {
            return null
        }
        const forward = anchorPage < focusPage
                        || (anchorPage === focusPage && anchorChar <= focusChar)
        const startPage = forward ? anchorPage : focusPage
        const startChar = forward ? anchorChar : focusChar
        const endPage = forward ? focusPage : anchorPage
        const endChar = forward ? focusChar : anchorChar
        if (page < startPage || page > endPage) {
            return null
        }
        const from = page === startPage ? startChar : 0
        const to = page === endPage ? endChar : doc.charCount(page) - 1
        return to >= from ? [from, to] : null
    }

    function selectedText() {
        let parts = []
        for (let page = Math.min(anchorPage, focusPage);
             hasSelection && page <= Math.max(anchorPage, focusPage); ++page) {
            const part = selectionOn(page)
            if (part) {
                parts.push(doc.text(page, part[0], part[1]))
            }
        }
        return parts.join("\n")
    }

    /// False when there was nothing to copy, so the key can go elsewhere.
    function copySelection() {
        const text = selectedText()
        if (text === "") {
            return false
        }
        doc.copyToClipboard(text)
        return true
    }

    /// The page and character under a point in the list's content, for a drag
    /// that has left the page it started on. Null in the gap between pages.
    function charAtContent(x, y) {
        const index = pages.indexAt(Math.max(0, Math.min(x, pages.contentWidth - 1)), y)
        const item = index >= 0 ? pages.itemAtIndex(index) : null
        if (!item) {
            return null
        }
        const p = item.paper.mapFromItem(pages.contentItem, x, y)
        const c = doc.charAt(index, p.x / item.paper.width, p.y / item.paper.height)
        return c >= 0 ? { page: index, char: c } : null
    }

    PdfDocument {
        id: doc
        source: root.filePath
    }

    function zoomBy(factor) {
        zoom = Math.max(minZoom, Math.min(maxZoom, zoom * factor))
    }

    function goToPage(number) {
        const index = Math.max(0, Math.min(doc.pageCount - 1, number - 1))
        pages.positionViewAtIndex(index, ListView.Beginning)
        currentPage = index + 1
    }

    // The document could not be opened at all: say why rather than showing
    // an empty grey area.
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 80
        spacing: 10
        visible: doc.error !== ""

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "📕"
            font.pixelSize: 44
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: doc.error
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            color: Theme.textSecondary
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        visible: doc.error === ""

        // The page list and its two scroll bars. The bars sit beside the
        // list, not over it: a page zoomed wider than the view would
        // otherwise run underneath them and show through the corner where
        // they meet.
        Item {
            id: pageArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            readonly property int barSize: 10
            /// The page is wider than the view. The content width follows the
            /// view's width, which never depends on a bar, so the strip this
            /// opens along the bottom cannot feed back into it.
            readonly property bool sideways: pages.contentWidth > pages.width

            ListView {
                id: pages
                objectName: "pdfPages"
                anchors.left: parent.left
                anchors.top: parent.top
                // The strip for the vertical bar is kept even when the bar is
                // off: if it came and went, the page would change width, which
                // changes whether the bar is needed, and the two could keep
                // flipping each other.
                width: parent.width - pageArea.barSize
                height: parent.height - (pageArea.sideways ? pageArea.barSize : 0)
                clip: true
                model: doc.pageCount
                spacing: 10
                // Two screens either side: enough that scrolling stays ahead of
                // the eye, few enough that a long document is not all rendered.
                // Before the first layout the height is briefly below zero,
                // the strip for the sideways bar taken from nothing.
                cacheBuffer: Math.round(Math.max(0, height) * 2)
                boundsBehavior: Flickable.StopAtBounds
                // A vertical ListView does not work out its own content width, so
                // past 100% the page would be cut off at the view's edge with no
                // way to reach the rest of it.
                contentWidth: Math.max(width, Math.round((width - 24) * root.zoom) + 24)
                flickableDirection: Flickable.HorizontalAndVerticalFlick

                ScrollBar.horizontal: ScrollBar {
                    id: pdfScrollX
                    objectName: "pdfScrollX"
                    // Placed in the strip below the list rather than over it.
                    parent: pageArea
                    anchors.left: pages.left
                    anchors.right: pages.right
                    anchors.top: pages.bottom
                    policy: pageArea.sideways ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    height: pageArea.barSize

                    contentItem: Rectangle {
                        implicitHeight: 8
                        radius: height / 2
                        color: pdfScrollX.pressed ? Theme.accent
                             : (pdfScrollX.hovered ? Theme.textSecondary : Theme.borderSubtle)
                    }

                    background: Rectangle {
                        color: Theme.bgInput
                        radius: height / 2
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    id: pdfScroll
                    objectName: "pdfScrollY"
                    // Placed in the strip beside the list rather than over it.
                    parent: pageArea
                    anchors.top: pages.top
                    anchors.left: pages.right
                    anchors.bottom: pages.bottom
                    // Same reasoning as the file list: AsNeeded lets the Basic
                    // style fade the bar out while nothing is touching it, so a
                    // document that had just opened looked like it had no more
                    // pages. On for as long as there is something to scroll.
                    policy: pages.contentHeight > pages.height ? ScrollBar.AlwaysOn
                                                               : ScrollBar.AlwaysOff
                    width: pageArea.barSize

                    contentItem: Rectangle {
                        implicitWidth: 8
                        radius: width / 2
                        color: pdfScroll.pressed ? Theme.accent
                             : (pdfScroll.hovered ? Theme.textSecondary : Theme.borderSubtle)
                    }

                    background: Rectangle {
                        color: Theme.bgInput
                        radius: width / 2
                    }
                }

                delegate: Item {
                    id: pageItem
                    required property int index
                    // As wide as the page when that is wider than the view, so
                    // there is something to scroll sideways through.
                    width: Math.max(pages.width, pageWidth + 24)
                    // Asking the document for the page's proportions costs no
                    // rendering, so the list can lay out before anything is drawn.
                    readonly property real aspect: doc.pageAspect(index) > 0 ? doc.pageAspect(index) : 0.7071
                    readonly property int pageWidth: Math.round((pages.width - 24) * root.zoom)
                    height: Math.round(pageWidth / aspect) + 8
                    /// The page itself, without the gap around it: what a
                    /// position on the page is measured against.
                    readonly property Item paper: paperRect

                    Rectangle {
                        id: paperRect
                        width: pageItem.pageWidth
                        height: pageItem.height - 8
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: "#ffffff"
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Image {
                            id: pageImage
                            anchors.fill: parent
                            anchors.margins: 1
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                            // The width is part of the request, so the page is
                            // drawn at the size it is shown rather than scaled.
                            sourceSize.width: pageItem.pageWidth
                            // Page first, then the encoded path: the id reaches
                            // the provider exactly as written here, so the
                            // separator has to be one a URL leaves alone.
                            source: root.filePath === ""
                                    ? ""
                                    : "image://pdfpage/" + pageItem.index
                                      + "/" + encodeURIComponent(root.filePath)
                        }

                        BusyIndicator {
                            anchors.centerIn: parent
                            running: pageImage.status === Image.Loading
                            visible: running
                        }

                        // This page's share of the selection.
                        Repeater {
                            model: {
                                const part = root.selectionOn(pageItem.index)
                                return part ? doc.textRects(pageItem.index, part[0], part[1]) : []
                            }
                            delegate: Rectangle {
                                required property var modelData
                                objectName: "selectionRect"
                                x: modelData.x * paperRect.width
                                y: modelData.y * paperRect.height
                                width: modelData.width * paperRect.width
                                height: modelData.height * paperRect.height
                                color: Theme.accent
                                opacity: 0.35
                            }
                        }

                        // A drag over the text selects it, as in any reader.
                        MouseArea {
                            id: selector
                            anchors.fill: parent
                            enabled: doc.textSelectable
                            cursorShape: Qt.IBeamCursor
                            // Otherwise the list takes the drag as a scroll. The
                            // wheel and the scroll bars still scroll.
                            preventStealing: true
                            property point pressedAt
                            property int pressedChar: -1
                            property bool dragging: false

                            onPressed: (mouse) => {
                                // A page number half typed is abandoned, so the
                                // keyboard is back with the preview for Ctrl+C.
                                if (root.typingInBar) {
                                    root.barEntryFinished()
                                }
                                pressedAt = Qt.point(mouse.x, mouse.y)
                                dragging = false
                                pressedChar = doc.charAt(pageItem.index, mouse.x / width,
                                                         mouse.y / height)
                            }
                            onPositionChanged: (mouse) => {
                                if (!dragging && Math.abs(mouse.x - pressedAt.x)
                                                 + Math.abs(mouse.y - pressedAt.y) < 4) {
                                    return
                                }
                                dragging = true
                                if (pressedChar < 0) {
                                    return
                                }
                                const p = mapToItem(pages.contentItem, mouse.x, mouse.y)
                                const hit = root.charAtContent(p.x, p.y)
                                if (hit) {
                                    root.select(pageItem.index, pressedChar, hit.page, hit.char)
                                }
                            }
                            // A click without a drag clears the selection.
                            onReleased: {
                                if (!dragging) {
                                    root.clearSelection()
                                }
                            }
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        text: pageItem.index + 1
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        color: Theme.textMuted
                    }
                }

                // Which page is "current" is whichever one covers the middle of
                // the view, which is what a reader would call the page they are on.
                onContentYChanged: {
                    // indexAt answers -1 when the point falls in the gap between
                    // two pages, which would read as "page 0" every few pixels.
                    const found = indexAt(width / 2, contentY + height / 2)
                    if (found >= 0) {
                        root.currentPage = found + 1
                    }
                }
            }
        }

        // ---- the bar along the bottom ----
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 8

            RowLayout {
                spacing: 6

                Text {
                    text: qsTr("Page:")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                }

                // Where to go: type a number and Enter goes there. It keeps
                // what was typed; where the view is now is shown beside it.
                TextField {
                    id: pageField
                    objectName: "pdfPageField"
                    property string beforeEdit: ""
                    Layout.preferredWidth: pageDigits.width + 14
                    Layout.preferredHeight: 22
                    padding: 0
                    leftPadding: 4
                    rightPadding: 4
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textPrimary
                    selectionColor: Theme.accent
                    selectByMouse: true
                    maximumLength: 6
                    validator: RegularExpressionValidator { regularExpression: /[0-9]*/ }

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.bgInput
                        border.color: pageField.activeFocus ? Theme.accent : Theme.borderSubtle
                        border.width: 1
                    }

                    // Room for the longest page number this document has.
                    TextMetrics {
                        id: pageDigits
                        font: pageField.font
                        text: "0".repeat(Math.max(3, String(doc.pageCount).length))
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            beforeEdit = text
                            selectAll()
                        }
                    }
                    onAccepted: {
                        const number = parseInt(text)
                        if (!isNaN(number)) {
                            root.goToPage(number)
                        }
                        root.barEntryFinished()
                    }
                    // Escape puts back what was there and leaves the field.
                    Keys.onEscapePressed: {
                        text = beforeEdit
                        root.barEntryFinished()
                    }
                }

                // Numbers only, so nothing here to translate.
                Text {
                    objectName: "pdfPageStatus"
                    text: "(" + root.currentPage + " / " + doc.pageCount + ")"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                }

                BarButton {
                    objectName: "pdfPreviousPage"
                    label: "←"
                    enabled: root.currentPage > 1
                    onClicked: root.goToPage(root.currentPage - 1)
                }
                BarButton {
                    objectName: "pdfNextPage"
                    label: "→"
                    enabled: root.currentPage < doc.pageCount
                    onClicked: root.goToPage(root.currentPage + 1)
                }
            }

            Item { Layout.fillWidth: true }

            component BarButton : Rectangle {
                property string label: ""
                signal clicked()
                Layout.preferredWidth: Math.max(28, barLabel.implicitWidth + 16)
                Layout.preferredHeight: 24
                radius: Theme.radiusSmall
                color: barMouse.containsMouse ? Theme.bgHover : Theme.bgHeader
                border.color: Theme.borderSubtle
                border.width: 1
                // Dimmed when there is nowhere to go, such as back from page 1.
                opacity: enabled ? 1 : 0.4

                Text {
                    id: barLabel
                    anchors.centerIn: parent
                    text: parent.label
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textPrimary
                }
                MouseArea {
                    id: barMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: parent.clicked()
                }
            }

            BarButton { label: "−"; onClicked: root.zoomBy(1 / 1.25) }

            // The zoom as a percentage. It follows the buttons and Ctrl+wheel;
            // type a number and Enter sets it.
            TextField {
                id: zoomField
                objectName: "pdfZoomField"
                text: Math.round(root.zoom * 100) + "%"
                Layout.preferredWidth: 54
                Layout.preferredHeight: 22
                padding: 0
                leftPadding: 4
                rightPadding: 4
                horizontalAlignment: TextInput.AlignHCenter
                verticalAlignment: TextInput.AlignVCenter
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textPrimary
                selectionColor: Theme.accent
                selectByMouse: true
                // Digits, and the % sign it shows, which may be left in.
                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,3}%?/ }

                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.bgInput
                    border.color: zoomField.activeFocus ? Theme.accent : Theme.borderSubtle
                    border.width: 1
                }

                // Typing replaces the binding; leaving the field puts it back,
                // so it shows the zoom in force whether or not the number was
                // used, and Escape needs nothing more than leaving.
                onActiveFocusChanged: {
                    if (activeFocus) {
                        selectAll()
                    } else {
                        text = Qt.binding(() => Math.round(root.zoom * 100) + "%")
                    }
                }
                onAccepted: {
                    const percent = parseInt(text)
                    if (!isNaN(percent)) {
                        root.zoom = Math.max(root.minZoom, Math.min(root.maxZoom, percent / 100))
                    }
                    root.barEntryFinished()
                }
                Keys.onEscapePressed: root.barEntryFinished()
            }

            BarButton { label: "+"; onClicked: root.zoomBy(1.25) }
            BarButton { label: qsTr("Fit"); onClicked: root.zoom = 1.0 }
        }
    }

    // Ctrl and the wheel is the zoom gesture every reader uses; without Ctrl
    // the wheel belongs to the list, so this handler lets it through.
    //
    // The scope is the page list rather than this panel: a Flickable does not
    // look at modifiers, so it takes Ctrl+wheel as an ordinary scroll, and a
    // handler on the panel behind it never sees the gesture. An item's handlers
    // are offered the event before the item itself.
    WheelHandler {
        parent: pages
        acceptedModifiers: Qt.ControlModifier
        onWheel: (event) => root.zoomBy(event.angleDelta.y > 0 ? 1.1 : 1 / 1.1)
    }

    /// Every key that moves the view goes through here, so the limits are
    /// written once.
    ///
    /// The list's content does not start at 0. It starts at originY, which
    /// moves as pages the list had only guessed the size of get built, and
    /// with pages of mixed sizes it can be thousands of pixels either way.
    /// So the two ends are left to the list itself, which knows exactly where
    /// its first and last pages are; the pages near the view are always built
    /// (cacheBuffer), so the estimate of the ends is exact by the time a step
    /// can reach them.
    function scrollBy(delta) {
        const target = pages.contentY + delta
        if (target <= pages.originY) {
            toStart()
        } else if (target >= pages.originY + pages.contentHeight - pages.height) {
            toEnd()
        } else {
            pages.contentY = target
        }
    }

    /// Left and right, for a page zoomed wider than the view. The content's
    /// width is set rather than estimated, so these limits are exact.
    function scrollSideways(delta) {
        const furthest = pages.originX + Math.max(0, pages.contentWidth - pages.width)
        pages.contentX = Math.max(pages.originX, Math.min(pages.contentX + delta, furthest))
    }

    function lineDown() { scrollBy(lineStep) }
    function lineUp() { scrollBy(-lineStep) }
    function lineLeft() { scrollSideways(-lineStep) }
    function lineRight() { scrollSideways(lineStep) }
    function pageDown() { scrollBy(pages.height * 0.9) }
    function pageUp() { scrollBy(-pages.height * 0.9) }
    function toStart() { pages.positionViewAtBeginning() }
    function toEnd() { pages.positionViewAtEnd() }
}
