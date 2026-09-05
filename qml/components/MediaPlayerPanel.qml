import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import QmlCommander

/**
 * Audio and video playback for the preview window.
 *
 * Lives in its own file because of the import above: a QML file that imports a
 * module which is not installed fails to compile, and Qt Multimedia is optional
 * here. The build adds this file only when the module is present, and the
 * preview window loads it through a Loader, so a Qt install without Multimedia
 * still builds and runs - it just shows a message instead of a player.
 */
Item {
    id: root
    objectName: "mediaPlayerPanel"

    /// Set by the preview window. Changing it loads and plays the new file.
    property url source: ""
    property bool isVideo: false

    /// The level to return to when the sound is turned back on. Silencing
    /// moves the slider to zero rather than setting a separate mute switch, so
    /// the bar always shows what is being heard.
    property real levelBeforeMute: 0.8

    /// Emitted when the picture is double clicked. The preview window decides
    /// what that means - this component has no idea how large it is allowed to
    /// get, and asking its parent directly would tie it to that one window.
    signal fullScreenToggleRequested()

    onSourceChanged: {
        player.stop()
        player.source = root.source
        if (root.source != "") {
            player.play()
        }
    }

    /// Stops playback and releases the file. The preview window calls this when
    /// it closes: without it the audio carries on after the window is gone, and
    /// the file stays open so it cannot be deleted or moved.
    function stopPlayback() {
        player.stop()
        player.source = ""
    }

    /// mm:ss, or h:mm:ss once a file is over an hour.
    function formatTime(ms) {
        if (isNaN(ms) || ms < 0) {
            return "0:00"
        }
        const total = Math.floor(ms / 1000)
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const seconds = total % 60
        const mm = (hours > 0 && minutes < 10) ? "0" + minutes : String(minutes)
        const ss = seconds < 10 ? "0" + seconds : String(seconds)
        return hours > 0 ? hours + ":" + mm + ":" + ss : mm + ":" + ss
    }

    MediaPlayer {
        id: player
        objectName: "mediaPlayer"
        audioOutput: AudioOutput {
            id: audio
            volume: volumeSlider.value
        }
        videoOutput: root.isVideo ? videoSurface : null

        onErrorOccurred: (error, errorString) => {
            errorText.text = errorString !== "" ? errorString
                                                : qsTr("This file cannot be played.")
            // console.warn reaches the log file in every build, unlike
            // console.log which is debug level and off in a release build.
            console.warn("media: cannot play", root.source, "-", errorString)
        }

        // Once the file is opened its tracks are known. Logged because "there
        // is no sound" has several causes that look identical on screen: a file
        // with no audio track at all, a track the decoder cannot handle, and an
        // output device that is not playing.
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia) {
                console.warn("media:", root.source,
                             "| audio track:", player.hasAudio,
                             "| video track:", player.hasVideo,
                             "| duration:", player.duration, "ms",
                             "| volume:", audio.volume,
                             "| muted:", audio.muted,
                             "| device:", audio.device ? audio.device.description : "none")

                // Which audio streams the file carries, and how each one is
                // encoded. A stream the build has no decoder for behaves the
                // same as a file with no sound in it, so the codec name is
                // the piece that tells the two apart.
                const tracks = player.audioTracks
                console.warn("media: audio tracks:", tracks.length,
                             "| active track index:", player.activeAudioTrack)
                for (let i = 0; i < tracks.length; ++i) {
                    console.warn("media:   track", i,
                                 "codec:", tracks[i].stringValue(MediaMetaData.AudioCodec),
                                 "language:", tracks[i].stringValue(MediaMetaData.Language))
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // ---- the picture, or a large icon for audio ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#000000"
            radius: Theme.radiusSmall

            VideoOutput {
                id: videoSurface
                anchors.fill: parent
                anchors.margins: 2
                visible: root.isVideo
                fillMode: VideoOutput.PreserveAspectFit
            }

            // Audio has nothing to show, so the space says what is playing
            // rather than sitting empty and black.
            ColumnLayout {
                anchors.centerIn: parent
                visible: !root.isVideo
                spacing: 6

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "♪"
                    font.pixelSize: 64
                    color: Theme.accent
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: player.metaData.stringValue(MediaMetaData.Title) !== ""
                          ? player.metaData.stringValue(MediaMetaData.Title) : ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    color: Theme.textPrimary
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: player.metaData.stringValue(MediaMetaData.AlbumArtist) !== ""
                          ? player.metaData.stringValue(MediaMetaData.AlbumArtist) : ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                }
            }

            // A file with no audio track is not an error, but without saying so
            // the only symptom is silence, which looks like a broken player.
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                visible: player.mediaStatus === MediaPlayer.LoadedMedia
                         || player.mediaStatus === MediaPlayer.BufferedMedia
                         ? !player.hasAudio : false
                text: qsTr("This file has no audio track.")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
            }

            Text {
                id: errorText
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: text !== ""
                color: Theme.textDanger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSmall
            }

            // Click the picture to pause or resume, and roll the wheel over it
            // to change the volume. Last child of the rectangle so it sits on
            // top of everything drawn in it; the video surface and the labels
            // do not take mouse input themselves, so nothing is covered up.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onClicked: root.togglePlay()

                // A double click is a first click that already paused, plus
                // this. Playing state is put back so the pair of clicks only
                // changes the size, and the second click of a double click
                // arrives here instead of as another plain click, so this runs
                // once and not twice.
                onDoubleClicked: {
                    root.togglePlay()
                    root.fullScreenToggleRequested()
                }

                onWheel: (wheel) => {
                    // One notch of an ordinary mouse wheel is 120 units, so
                    // this is a 5% step per notch, and a trackpad's smaller
                    // movements scale down with it.
                    root.changeVolume(wheel.angleDelta.y / 120 * 0.05)
                }
            }
        }

        // ---- position ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: root.formatTime(player.position)
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
            }

            Slider {
                id: seekBar
                objectName: "seekBar"
                // Clicking a control normally gives it the keyboard, and a
                // Slider uses the arrow keys itself. That made a single click
                // on either bar swallow the arrows from then on: the preview
                // window never saw them again, so seeking and volume stopped
                // working and nothing brought them back. Neither bar needs the
                // keyboard - the arrow keys already drive both - so neither
                // takes it.
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, player.duration)
                // While the handle is held, the slider is the one in charge -
                // otherwise the position update fights the drag and the handle
                // jumps back under the cursor.
                value: pressed ? value : player.position
                onMoved: player.position = value
                enabled: player.seekable

                // The stock style paints the part already played darker than
                // the part still to come, which reads backwards: the section
                // that is done looks like the empty one. Light on the left,
                // dark on the right.
                background: Rectangle {
                    x: seekBar.leftPadding
                    y: seekBar.topPadding + seekBar.availableHeight / 2 - height / 2
                    width: seekBar.availableWidth
                    height: 4
                    radius: 2
                    // Plain grey for the part not yet reached, so it stays
                    // visible against the window rather than sinking into it.
                    color: Theme.textMuted

                    Rectangle {
                        width: seekBar.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: seekBar.enabled ? Theme.textPrimary : Theme.borderSubtle
                    }
                }

                handle: Rectangle {
                    x: seekBar.leftPadding
                       + seekBar.visualPosition * (seekBar.availableWidth - width)
                    y: seekBar.topPadding + seekBar.availableHeight / 2 - height / 2
                    implicitWidth: 14
                    implicitHeight: 14
                    radius: width / 2
                    color: seekBar.pressed ? Theme.accent : Theme.textPrimary
                    border.color: Theme.bgInput
                    border.width: 1
                }
            }

            Text {
                text: root.formatTime(player.duration)
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
            }
        }

        // ---- transport ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                id: playButton
                objectName: "playButton"
                width: 92
                height: 30
                radius: Theme.radiusSmall
                color: playMouse.containsMouse ? Theme.accentHover : Theme.accent
                border.color: Theme.textPrimary
                border.width: playButton.activeFocus ? 2 : 0

                Text {
                    anchors.centerIn: parent
                    text: player.playbackState === MediaPlayer.PlayingState
                          ? "⏸ Pause" : "▶ Play"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    color: "#0f172a"
                }

                MouseArea {
                    id: playMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.togglePlay()
                }
            }

            Rectangle {
                width: 72
                height: 30
                radius: Theme.radiusSmall
                color: stopMouse.containsMouse ? Theme.bgHover : Theme.bgHeader
                border.color: Theme.borderSubtle
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "■ Stop"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textPrimary
                }

                MouseArea {
                    id: stopMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: player.stop()
                }
            }

            Item { Layout.fillWidth: true }

            // Click the speaker to silence the sound and to bring it back at
            // the level it was.
            Text {
                id: muteIcon
                objectName: "muteButton"
                text: volumeSlider.value === 0 ? "🔇" : "🔈"
                font.pixelSize: 14
                color: muteMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary

                MouseArea {
                    id: muteMouse
                    anchors.fill: parent
                    // The glyph is small, so the clickable area is grown a
                    // little past it to be comfortable to hit.
                    anchors.margins: -5
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toggleMute()
                }
            }

            Slider {
                id: volumeSlider
                objectName: "volumeSlider"
                focusPolicy: Qt.NoFocus
                Layout.preferredWidth: 110
                from: 0
                to: 1
                value: 0.8

                background: Rectangle {
                    x: volumeSlider.leftPadding
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: volumeSlider.availableWidth
                    height: 4
                    radius: 2
                    // Plain grey for the part not yet reached, so it stays
                    // visible against the window rather than sinking into it.
                    color: Theme.textMuted

                    Rectangle {
                        width: volumeSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: Theme.textPrimary
                    }
                }

                handle: Rectangle {
                    x: volumeSlider.leftPadding
                       + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    implicitWidth: 14
                    implicitHeight: 14
                    radius: width / 2
                    color: volumeSlider.pressed ? Theme.accent : Theme.textPrimary
                    border.color: Theme.bgInput
                    border.width: 1
                }
            }
        }
    }

    /// Silence the sound, or bring it back at the level it was before. Half
    /// volume is used if it was already at zero when the file was opened, so
    /// the button always does something audible.
    function toggleMute() {
        if (volumeSlider.value > 0) {
            root.levelBeforeMute = volumeSlider.value
            volumeSlider.value = 0
        } else {
            volumeSlider.value = root.levelBeforeMute > 0 ? root.levelBeforeMute : 0.5
        }
    }

    /// Raise or lower the volume by this much, keeping it between silent and
    /// full. Bound to the up and down arrow keys, and to the wheel over the
    /// picture, so the two cannot drift apart.
    function changeVolume(delta) {
        volumeSlider.value = Math.max(0, Math.min(1, volumeSlider.value + delta))
    }

    /// Move the play position by this many milliseconds, forwards or back,
    /// stopping at the two ends of the file. Bound to the left and right arrow
    /// keys in the preview window.
    function seekBy(ms) {
        if (!player.seekable || player.duration <= 0) {
            return
        }
        player.position = Math.max(0, Math.min(player.duration, player.position + ms))
    }

    /// Play or pause, whichever the current state calls for. Bound to the play
    /// button and to the space bar in the preview window.
    function togglePlay() {
        if (player.playbackState === MediaPlayer.PlayingState) {
            player.pause()
        } else {
            player.play()
        }
    }
}
