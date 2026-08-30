import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root

    required property var controller

    title: qsTr("Receiving")
    // Fill the window in receiver mode; the video should dominate.
    anchors.fill: parent

    // The video surface. Displays the latest decoded frame from the provider.
    Rectangle {
        id: videoSurface
        anchors.fill: parent
        color: "black"

        Image {
            id: videoImage
            // Fill the parent and let PreserveAspectFit scale + center the
            // frame correctly. Manual width/height math overflowed the window
            // and clipped (left half black, drag to reveal the rest).
            anchors.fill: parent
            source: root.controller.currentFrame
            fillMode: Image.PreserveAspectFit
            cache: false
            asynchronous: false
            // Re-fetch from the provider whenever a new frame arrives.
            Connections {
                target: root.controller
                function onFrameReady() {
                    videoImage.source = ""
                    videoImage.source = root.controller.currentFrame
                }
            }
        }

        // Input: forward mouse/touch to the sender as normalized coordinates.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => root.controller.sendTouch("began", mouse.x / width, mouse.y / height)
            onPositionChanged: mouse => {
                if (mouse.buttons & Qt.LeftButton) {
                    root.controller.sendTouch("moved", mouse.x / width, mouse.y / height)
                }
            }
            onReleased: mouse => root.controller.sendTouch("ended", mouse.x / width, mouse.y / height)
            onWheel: wheel => root.controller.sendScroll(wheel.angleDelta.x / 120.0, wheel.angleDelta.y / 120.0)
            onDoubleClicked: root.toggleFullscreen()
        }
    }

    // F11 toggles borderless fullscreen.
    Shortcut {
        sequence: "F11"
        onActivated: root.toggleFullscreen()
    }

    // Status overlay (only while waiting for a sender).
    Controls.Label {
        anchors.centerIn: parent
        text: root.controller.status + "\n" + root.controller.detail
        color: "white"
        horizontalAlignment: Text.AlignHCenter
        visible: !root.controller.connected
    }

    // Floating controls — hidden in fullscreen so the video fills the screen.
    footer: Controls.ToolBar {
        visible: !root.isFullscreen
        RowLayout {
            anchors.fill: parent
            Controls.Label {
                Layout.fillWidth: true
                text: root.controller.detail
                elide: Text.ElideRight
            }
            Controls.Button {
                text: qsTr("Fullscreen")
                icon.name: "view-fullscreen"
                onClicked: root.toggleFullscreen()
            }
            Controls.Button {
                text: qsTr("Disconnect")
                onClicked: root.controller.disconnectDevice()
            }
        }
    }

    property bool isFullscreen: false

    function toggleFullscreen() {
        const window = root.Window.window
        if (!window) return
        if (window.visibility === Window.FullScreen) {
            window.visibility = Window.Windowed
            root.isFullscreen = false
        } else {
            window.visibility = Window.FullScreen
            root.isFullscreen = true
        }
    }
}
