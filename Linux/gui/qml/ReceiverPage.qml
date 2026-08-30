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
            // Aspect-correct fill: track the ACTUAL image size (not sourceSize,
            // which is 0 before the first frame loads and would make the ratio
            // NaN/invisible).
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height * (videoImage.implicitWidth / Math.max(1, videoImage.implicitHeight)))
            height: Math.min(parent.height, parent.width * (videoImage.implicitHeight / Math.max(1, videoImage.implicitWidth)))
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

    // Status overlay (only while waiting for a sender).
    Controls.Label {
        anchors.centerIn: parent
        text: root.controller.status + "\n" + root.controller.detail
        color: "white"
        horizontalAlignment: Text.AlignHCenter
        visible: !root.controller.connected
    }

    // Floating controls — fade out so the video stays unobstructed.
    footer: Controls.ToolBar {
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

    function toggleFullscreen() {
        const window = root.Window.window
        if (!window) return
        window.visibility = window.visibility === Window.FullScreen
            ? Window.Windowed
            : Window.FullScreen
    }
}
