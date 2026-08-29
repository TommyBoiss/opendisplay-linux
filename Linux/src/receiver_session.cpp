#include "opendisplay/receiver_session.hpp"

#include "opendisplay/log.hpp"
#include "opendisplay/wire.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace od {
namespace {

constexpr std::uint32_t kMaxControlSize = 1U << 20U;
constexpr std::int64_t kPingIntervalMs = 2000;
constexpr std::int64_t kWatchdogMs = 5000;
constexpr std::int64_t kHelloDebounceMs = 300;

/// A payload is a control message iff it is small, starts with '{', and
/// contains no NUL byte (video frames always contain Annex B start codes).
bool looksLikeControl(const std::string_view payload) {
    return payload.size() < 32'768 && !payload.empty() && payload.front() == '{'
        && payload.find('\0') == std::string_view::npos;
}

}  // namespace

ReceiverSession::~ReceiverSession() { stop(); }

void ReceiverSession::start(const std::uint16_t port, const PhoneInfo panel,
                            FrameCallback onFrame, ClosedCallback onClosed) {
    stop();
    panel_ = panel;
    onFrame_ = std::move(onFrame);
    onClosed_ = std::move(onClosed);
    listener_ = listenTcp(port);
    // Query the actual bound port (needed when started with port 0).
    sockaddr_in bound{};
    socklen_t boundLength = sizeof(bound);
    if (::getsockname(listener_.fd(), reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
        boundPort_.store(ntohs(bound.sin_port));
    }
    running_.store(true);
    acceptThread_ = std::thread(&ReceiverSession::acceptLoop, this);
}

std::uint16_t ReceiverSession::boundPort() const { return boundPort_.load(); }

bool ReceiverSession::tick() {
    if (!running_.load()) {
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (connected_.load()) {
        // Watchdog: if nothing arrived for a while, drop the half-open link.
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActivity_).count()
            > kWatchdogMs) {
            closeWith("receiver timed out waiting for the sender");
            return false;
        }
        // Periodic ping for clock sync / liveness.
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPingSent_).count()
            >= kPingIntervalMs) {
            lastPingSent_ = now;
            send(wire::frame("{\"type\":\"ping\",\"t\":" + std::to_string(wallClockMs()) + "}"));
        }
        // Debounced hello re-send on panel change.
        if (helloPending_
            && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHelloSent_).count()
                >= kHelloDebounceMs) {
            helloPending_ = false;
            sendHello();
        }
    }
    return true;
}

void ReceiverSession::stop() {
    running_.store(false);
    connected_.store(false);
    if (listener_.valid()) {
        listener_.close();
    }
    if (socket_.valid()) {
        socket_.close();
    }
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    if (readThread_.joinable()) {
        readThread_.join();
    }
}

void ReceiverSession::sendTouch(const std::string& phase, const double x, const double y) {
    if (!connected_.load()) {
        return;
    }
    QJsonObject object{{"type", "touch"}, {"phase", QString::fromStdString(phase)},
                       {"x", x}, {"y", y}};
    send(wire::frame(QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString()));
}

void ReceiverSession::sendScroll(const double dx, const double dy) {
    if (!connected_.load()) {
        return;
    }
    QJsonObject object{{"type", "scroll"}, {"dx", dx}, {"dy", dy}};
    send(wire::frame(QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString()));
}

void ReceiverSession::requestKeyframe() {
    if (!connected_.load()) {
        return;
    }
    send(wire::frame("{\"type\":\"kf\"}"));
}

void ReceiverSession::setNativePanel(const int width, const int height, const double scale) {
    {
        std::lock_guard lock(stateMutex_);
        if (panel_.pixelsWide == width && panel_.pixelsHigh == height) {
            return;
        }
        panel_.pixelsWide = width;
        panel_.pixelsHigh = height;
        panel_.scale = scale;
        helloPending_ = true;
        lastHelloSent_ = std::chrono::steady_clock::now();
    }
}

void ReceiverSession::acceptLoop() {
    while (running_.load()) {
        Socket peer = acceptConnection(listener_);
        if (!peer.valid()) {
            if (running_.load()) {
                continue;
            }
            break;
        }
        // A new connection cancels any previous one.
        if (socket_.valid()) {
            socket_.close();
        }
        if (readThread_.joinable()) {
            readThread_.join();
        }
        socket_ = std::move(peer);
        connected_.store(true);
        lastActivity_ = std::chrono::steady_clock::now();
        lastPingSent_ = lastActivity_;
        sendHello();
        readThread_ = std::thread(&ReceiverSession::readLoop, this);
    }
}

void ReceiverSession::readLoop() {
    std::array<char, 4> header{};
    while (running_.load() && connected_.load()) {
        if (!socket_.readExact(header)) {
            closeWith("sender disconnected");
            return;
        }
        const auto length = wire::decodeLength(header);
        if (!length.has_value()) {
            closeWith("invalid frame length from sender");
            return;
        }
        std::string payload(static_cast<std::size_t>(*length), '\0');
        if (!socket_.readExact(payload)) {
            closeWith("sender disconnected mid-frame");
            return;
        }
        lastActivity_ = std::chrono::steady_clock::now();
        if (looksLikeControl(payload)) {
            handleControl(payload);
        } else {
            handleVideo(payload);
        }
    }
}

void ReceiverSession::handleControl(const std::string_view payload) {
    const auto object = wire::parseJson(payload);
    if (!object.has_value()) {
        return;
    }
    const auto type = object->value("type").toString();
    if (type == QStringLiteral("welcome")) {
        debug("Receiver: sender welcomed us (pv="
              + object->value("pv").toString().toStdString() + ")");
    } else if (type == QStringLiteral("pong")) {
        // Clock sync sample; not needed for display, ignore.
    } else if (type == QStringLiteral("ping")) {
        // Sender liveness ping; reply with pong.
        const double t = object->value("t").toDouble();
        send(wire::frame(wire::pong(t, static_cast<double>(wallClockMs()))));
    } else if (type == QStringLiteral("updateRequired")) {
        log("Sender requested an update: "
            + object->value("message").toString().toStdString());
    }
}

void ReceiverSession::handleVideo(const std::string_view payload) {
    // Strip the {"cap":..,"snd":..} telemetry prefix up to the first start code.
    const auto start = payload.find('\0');
    if (start == std::string_view::npos) {
        return;
    }
    // Find the first 00 00 00 01 (or 00 00 01) start code.
    std::size_t annexB = std::string_view::npos;
    for (std::size_t i = start; i + 3 < payload.size(); ++i) {
        if (payload[i] == 0 && payload[i + 1] == 0
            && (payload[i + 2] == 1
                || (i + 3 < payload.size() && payload[i + 2] == 0 && payload[i + 3] == 1))) {
            annexB = i;
            break;
        }
    }
    if (annexB == std::string_view::npos) {
        return;
    }
    // Parse the telemetry prefix for timestamps (best-effort).
    std::int64_t cap = 0;
    std::int64_t snd = 0;
    const auto telemetry = payload.substr(0, annexB);
    if (const auto object = wire::parseJson(telemetry); object.has_value()) {
        cap = static_cast<std::int64_t>(object->value("cap").toDouble());
        snd = static_cast<std::int64_t>(object->value("snd").toDouble());
    }
    if (onFrame_) {
        onFrame_(ReceivedFrame{.capturedAtMs = cap, .sentAtMs = snd, .width = 0, .height = 0,
                               .bgra = std::string(payload.substr(annexB))});
    }
}

void ReceiverSession::sendHello() {
    send(wire::frame(wire::hello(panel_)));
    lastHelloSent_ = std::chrono::steady_clock::now();
}

bool ReceiverSession::send(const std::string_view payload) {
    if (!connected_.load()) {
        return false;
    }
    std::lock_guard lock(sendMutex_);
    return socket_.writeAll(payload);
}

void ReceiverSession::closeWith(const std::string& reason) {
    if (!connected_.exchange(false)) {
        return;
    }
    closeReason_ = reason;
    if (socket_.valid()) {
        socket_.close();
    }
    if (onClosed_) {
        onClosed_(reason);
    }
}

}  // namespace od
