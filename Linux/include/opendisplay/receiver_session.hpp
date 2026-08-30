#pragma once

#include "opendisplay/socket.hpp"
#include "opendisplay/types.hpp"
#include "opendisplay/usbmux_receiver.hpp"

#include <QJsonObject>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace od {

/// A decoded video frame delivered to the GUI for display.
struct ReceivedFrame {
    std::int64_t capturedAtMs = 0;
    std::int64_t sentAtMs = 0;
    int width = 0;
    int height = 0;
    std::string bgra;  // BGRA, width*height*4 bytes
};

/// Cursor state delivered to the GUI (receiver mode). The Mac sender streams
/// a cursor sprite (base64 PNG) plus a normalized position; the receiver
/// renders the sprite over the video at the given coordinates.
struct CursorState {
    bool visible = false;
    double x = 0.0;  // normalized [0,1] within the display
    double y = 0.0;  // normalized [0,1] within the display
    double width = 0.0;    // normalized sprite width
    double height = 0.0;   // normalized sprite height
    double anchorX = 0.0;  // normalized hotspot within the sprite
    double anchorY = 0.0;  // normalized hotspot within the sprite
    std::string png;       // base64 PNG sprite (empty until cursorImg arrives)
};

/// Listens for a single sender, performs the receiver side of the wire
/// handshake, and delivers decoded frames + control events. Mirrors the
/// threading model of Session: a dedicated accept/read thread plus a
/// tick() driven by the Qt main loop.
class ReceiverSession {
public:
    using FrameCallback = std::function<void(ReceivedFrame)>;
    using CursorCallback = std::function<void(CursorState)>;
    using ClosedCallback = std::function<void(const std::string& reason)>;

    ReceiverSession() = default;
    ~ReceiverSession();
    ReceiverSession(const ReceiverSession&) = delete;
    ReceiverSession& operator=(const ReceiverSession&) = delete;

    /// Bind and listen on `port` (0.0.0.0). Advertises the given panel size.
    void start(std::uint16_t port, PhoneInfo panel, FrameCallback onFrame,
               CursorCallback onCursor, ClosedCallback onClosed);
    /// Speak the usbmuxd server protocol on `port`, so a sender using
    /// libusbmuxd can reach this receiver with USBMUXD_SOCKET_ADDRESS.
    void startUsbmux(std::uint16_t port, PhoneInfo panel, FrameCallback onFrame,
                    CursorCallback onCursor, ClosedCallback onClosed);
    /// Called by the Qt main loop. Returns false after the connection closes.
    bool tick();
    void stop();

    /// Send input back to the sender. Coordinates are normalized [0,1].
    void sendTouch(const std::string& phase, double x, double y);
    void sendScroll(double dx, double dy);
    void requestKeyframe();

    /// Update the advertised panel size (e.g. on window resize) and re-send
    /// hello so the sender rebuilds its pipeline.
    void setNativePanel(int width, int height, double scale);

    [[nodiscard]] bool connected() const { return connected_.load(); }
    /// The port the listener actually bound (useful when started with port 0).
    [[nodiscard]] std::uint16_t boundPort() const;
    /// The port the usbmuxd-protocol listener bound (0 when not running).
    [[nodiscard]] std::uint16_t usbmuxPort() const;

private:
    void acceptLoop();
    void readLoop();
    bool send(std::string_view payload);
    void handleControl(std::string_view payload);
    void handleVideo(std::string_view payload);
    void handleCursor(const QJsonObject& object);
    void handleCursorImage(const QJsonObject& object);
    void sendHello();
    void closeWith(const std::string& reason);
    /// Take ownership of an already-mux-negotiated socket (usbmuxd path).
    void adoptSocket(Socket peer);
    void usbmuxAcceptLoop();

    Socket listener_;
    std::unique_ptr<UsbmuxReceiver> usbmux_;
    std::thread usbmuxAcceptThread_;
    Socket socket_;
    std::thread acceptThread_;
    std::thread readThread_;
    std::atomic_bool running_ = false;
    std::atomic_bool connected_ = false;
    std::atomic<std::uint16_t> boundPort_ = 0;
    std::atomic<std::uint16_t> usbmuxPort_ = 0;
    std::mutex sendMutex_;
    std::mutex stateMutex_;
    std::condition_variable stateCondition_;
    PhoneInfo panel_;
    FrameCallback onFrame_;
    CursorCallback onCursor_;
    ClosedCallback onClosed_;
    std::string closeReason_;
    std::chrono::steady_clock::time_point lastActivity_{};
    std::chrono::steady_clock::time_point lastPingSent_{};
    std::chrono::steady_clock::time_point lastHelloSent_{};
    bool helloPending_ = false;
};

}  // namespace od

Q_DECLARE_METATYPE(od::CursorState)
