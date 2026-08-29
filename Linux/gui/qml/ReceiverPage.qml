import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root

    required property var controller

    title: qsTr("Receiving")

    // The video surface. Displays the latest decoded frame from the controller.
    Rectangle {
        id: videoSurface
        anchors.fill: parent
        color: "black"

        Image {
            id: videoImage
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height * (sourceSize.width / Math.max(1, sourceSize.height)))
            height: Math.min(parent.height, parent.width * (sourceSize.height / Math.max(1, sourceSize.width)))
            source: root.controller.currentFrame
            fillMode: Image.PreserveAspectFit
            cache: false
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
        }
    }

    // Status overlay.
    Controls.Label {
        anchors.centerIn: parent
        text: root.controller.status + "\n" + root.controller.detail
        color: "white"
        horizontalAlignment: Text.AlignHCenter
        visible: !root.controller.connected
    }

    footer: Controls.ToolBar {
        RowLayout {
            anchors.fill: parent
            Controls.Label {
                Layout.fillWidth: true
                text: root.controller.detail
                elide: Text.ElideRight
            }
            Controls.Button {
                text: qsTr("Disconnect")
                onClicked: root.controller.disconnectDevice()
            }
        }
    }
}
