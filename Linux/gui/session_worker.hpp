#pragma once

#include "opendisplay/avahi_publish.hpp"
#include "opendisplay/ffmpeg_decoder.hpp"
#include "opendisplay/receiver_session.hpp"
#include "opendisplay/session.hpp"
#include "opendisplay/types.hpp"

#include <QObject>

#include <memory>

class QTimer;

namespace od::gui {

class SessionWorker final : public QObject {
    Q_OBJECT

public:
    explicit SessionWorker(QObject* parent = nullptr);

public slots:
    void start(od::Options options);
    void stop();
    /// Update the advertised panel size (receiver mode) on window resize.
    void setPanel(int width, int height, double scale);
    /// Send a touch event back to the sender (receiver mode).
    void sendTouch(const QString& phase, double x, double y);
    /// Send a scroll event back to the sender (receiver mode).
    void sendScroll(double dx, double dy);

signals:
    void stateChanged(const QString& status, const QString& detail,
                      bool connected, bool busy);
    /// Emitted when a decoded frame is ready for display (receiver mode).
    void frameReady(const od::DecodedFrame& frame);
    /// Emitted when the sender updates the cursor sprite or position.
    void cursorChanged(const od::CursorState& cursor);
    /// Emitted when the decoded stream's actual dimensions change, so the GUI
    /// can resize the advertised panel to match what the sender streams.
    void streamSizeChanged(int width, int height);
    /// Emitted when the receiver window is resized so the panel can be updated.
    void panelChanged(int width, int height, double scale);

private slots:
    void tick();

private:
    void startSender(od::Options options);
    void startReceiver(od::Options options);

    QTimer* timer_ = nullptr;
    std::unique_ptr<od::Session> session_;
    std::unique_ptr<od::ReceiverSession> receiver_;
    std::unique_ptr<od::FfmpegDecoder> decoder_;
    std::unique_ptr<od::ServiceAdvertiser> advertiser_;
    od::SessionRole role_ = od::SessionRole::Sender;
    bool receiverConnected_ = false;
    int lastFrameWidth_ = 0;
    int lastFrameHeight_ = 0;
};

}  // namespace od::gui
