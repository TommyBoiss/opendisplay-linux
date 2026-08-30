#include "session_worker.hpp"

#include "opendisplay/avahi_publish.hpp"
#include "opendisplay/desktop_backend_factory.hpp"
#include "opendisplay/discovery.hpp"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>

namespace od::gui {
namespace {

int primaryScreenWidth() {
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        // Physical (max) pixel count: QScreen::size() is logical pixels, so
        // multiply by the device pixel ratio to hit the panel's native
        // resolution on HiDPI surfaces (e.g. a Surface at 150–200% scaling).
        return qRound(screen->size().width() * screen->devicePixelRatio());
    }
    return 1920;
}

int primaryScreenHeight() {
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        return qRound(screen->size().height() * screen->devicePixelRatio());
    }
    return 1080;
}

}  // namespace

SessionWorker::SessionWorker(QObject* parent) : QObject(parent), timer_(new QTimer(this)) {
    timer_->setInterval(std::chrono::milliseconds(20));
    connect(timer_, &QTimer::timeout, this, &SessionWorker::tick);
    // Follow the DE's sensor-driven screen rotation (tablet/Surface): when the
    // primary screen orientation changes, re-advertise the panel in the new
    // orientation. The worker runs on a separate thread, so the connection is
    // queued; orientationChanged is a cross-thread signal, which is fine.
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        connect(screen, &QScreen::orientationChanged, this,
                &SessionWorker::onOrientationChanged, Qt::QueuedConnection);
    }
}

void SessionWorker::start(od::Options options) {
    if (session_ || receiver_) return;
    role_ = options.role;
    if (role_ == od::SessionRole::Receiver) {
        startReceiver(std::move(options));
    } else {
        startSender(std::move(options));
    }
}

void SessionWorker::startSender(od::Options options) {
    emit stateChanged(QStringLiteral("Connecting"),
                      QStringLiteral("Searching for an OpenDisplay receiver…"), false, true);
    try {
        const auto endpoint = od::chooseEndpoint(options);
        const QString endpointName = QString::fromStdString(
            endpoint.name.empty() ? (endpoint.kind == od::TransportKind::Usb
                                         ? endpoint.udid
                                         : endpoint.host)
                                  : endpoint.name);
        emit stateChanged(QStringLiteral("Connecting"),
                          QStringLiteral("Negotiating with %1…").arg(endpointName), false, true);
        session_ = std::make_unique<od::Session>(
            options, od::makeDesktopBackend(options.compositor));
        session_->start(endpoint);
        timer_->start();
        emit stateChanged(QStringLiteral("Connected"),
                          QStringLiteral("Streaming to %1").arg(endpointName), true, false);
    } catch (const std::exception& error) {
        timer_->stop();
        if (session_) session_->stop();
        session_.reset();
        emit stateChanged(QStringLiteral("Connection failed"),
                          QString::fromUtf8(error.what()), false, false);
    }
}

void SessionWorker::startReceiver(od::Options options) {
    receiverConnected_ = false;
    emit stateChanged(QStringLiteral("Listening"),
                      QStringLiteral("Waiting for a sender on port %1…").arg(options.port),
                      false, true);
    try {
        // Advertise the ACTUAL primary screen resolution instead of a
        // hardcoded 1920x1080 panel, so the Mac sizes its virtual display to
        // match this surface rather than a small fixed panel.
        od::PhoneInfo panel{
            .pixelsWide = options.display.virtualResolution.has_value()
                ? options.display.virtualResolution->width
                : primaryScreenWidth(),
            .pixelsHigh = options.display.virtualResolution.has_value()
                ? options.display.virtualResolution->height
                : primaryScreenHeight(),
            .scale = 1.0,
            .device = "Linux",
            .installId = "linux-receiver",
            .protocolVersion = 2};
        // Record the fixed native long/short dims (iPad-style) so an
        // orientation change can swap them for a stable portrait/landscape
        // without re-reading the live screen resolution.
        panelLong_ = std::max(panel.pixelsWide, panel.pixelsHigh);
        panelShort_ = std::min(panel.pixelsWide, panel.pixelsHigh);
        panelInitialised_ = true;
        // Advertise landscape (long x short) initially; orientation changes
        // swap to portrait via onOrientationChanged.
        panel.pixelsWide = panelLong_;
        panel.pixelsHigh = panelShort_;
        receiver_ = std::make_unique<od::ReceiverSession>();
        decoder_ = std::make_unique<od::FfmpegDecoder>();
        // usbmuxd protocol (senders reach us with USBMUXD_SOCKET_ADDRESS) or
        // plain TCP, depending on the selected transport.
        if (options.transport == od::TransportKind::Usb) {
            receiver_->startUsbmux(options.port, panel,
                                   [this](const od::ReceivedFrame& frame) {
                                       decoder_->submit(frame.bgra);
                                   },
                                   [this](const od::CursorState& cursor) {
                                       emit cursorChanged(cursor);
                                   },
                                   [this](const std::string& reason) {
                                       emit stateChanged(QStringLiteral("Disconnected"),
                                                         QString::fromStdString(reason),
                                                         false, false);
                                   });
        } else {
            receiver_->start(options.port, panel,
                            [this](const od::ReceivedFrame& frame) {
                                decoder_->submit(frame.bgra);
                            },
                            [this](const od::CursorState& cursor) {
                                emit cursorChanged(cursor);
                            },
                            [this](const std::string& reason) {
                                emit stateChanged(QStringLiteral("Disconnected"),
                                                  QString::fromStdString(reason),
                                                  false, false);
                            });
            // Advertise over mDNS for the lifetime of the session so senders
            // (the Mac app and the Linux CLI) keep discovering us.
            advertiser_ = std::make_unique<od::ServiceAdvertiser>();
            const std::string publishError = advertiser_->start(
                options.serviceName, options.port, "linux-receiver");
            if (!publishError.empty()) {
                advertiser_.reset();
                emit stateChanged(QStringLiteral("Connection failed"),
                                  QString::fromStdString(publishError), false, false);
                return;
            }
        }
        decoder_->start([this](const od::DecodedFrame& frame) {
            // Report the actual stream dimensions once, so the GUI can resize
            // the advertised panel to match what the sender is streaming.
            if (frame.width > 0 && frame.height > 0
                && (frame.width != lastFrameWidth_ || frame.height != lastFrameHeight_)) {
                lastFrameWidth_ = frame.width;
                lastFrameHeight_ = frame.height;
                emit streamSizeChanged(frame.width, frame.height);
            }
            emit frameReady(frame);
        }, panel.pixelsWide, panel.pixelsHigh);
        timer_->start();
        emit stateChanged(QStringLiteral("Listening"),
                          options.transport == od::TransportKind::Usb
                              ? QStringLiteral(
                                    "Waiting for a sender (usbmuxd) on port %1…")
                                    .arg(options.port)
                              : QStringLiteral("Waiting for a sender on port %1…")
                                    .arg(options.port),
                          false, true);
    } catch (const std::exception& error) {
        timer_->stop();
        if (receiver_) receiver_->stop();
        receiver_.reset();
        if (decoder_) decoder_->stop();
        decoder_.reset();
        emit stateChanged(QStringLiteral("Connection failed"),
                          QString::fromUtf8(error.what()), false, false);
    }
}

void SessionWorker::tick() {
    if (receiver_) {
        try {
            // Transition to "Connected" the moment a sender dials in, so the
            // GUI reflects the live session instead of staying on "Listening".
            if (receiver_->connected() && !receiverConnected_) {
                receiverConnected_ = true;
                emit stateChanged(QStringLiteral("Connected"),
                                  QStringLiteral("Streaming from the sender."), true, false);
            }
            if (receiver_->tick()) return;
            timer_->stop();
            receiver_->stop();
            receiver_.reset();
            if (decoder_) decoder_->stop();
            decoder_.reset();
            receiverConnected_ = false;
            emit stateChanged(QStringLiteral("Disconnected"),
                              QStringLiteral("The sender ended the connection."), false, false);
        } catch (const std::exception& error) {
            timer_->stop();
            receiver_->stop();
            receiver_.reset();
            if (decoder_) decoder_->stop();
            decoder_.reset();
            receiverConnected_ = false;
            emit stateChanged(QStringLiteral("Connection failed"),
                              QString::fromUtf8(error.what()), false, false);
        }
        return;
    }
    if (!session_) {
        timer_->stop();
        return;
    }
    try {
        if (session_->tick()) return;
        timer_->stop();
        session_->stop();
        session_.reset();
        emit stateChanged(QStringLiteral("Disconnected"),
                          QStringLiteral("The receiver ended the connection."), false, false);
    } catch (const std::exception& error) {
        timer_->stop();
        session_->stop();
        session_.reset();
        emit stateChanged(QStringLiteral("Connection failed"),
                          QString::fromUtf8(error.what()), false, false);
    }
}

void SessionWorker::setPanel(const int width, const int height, const double scale) {
    if (receiver_) {
        receiver_->setNativePanel(width, height, scale);
    }
}

void SessionWorker::onOrientationChanged(const Qt::ScreenOrientation orientation) {
    if (!receiver_ || !panelInitialised_) return;
    // Match the iPad receiver: keep a fixed native panel and simply swap
    // long/short on orientation change. This is more stable than re-reading
    // the live screen resolution, which varies per orientation/mode on many
    // compositors. If the DE hasn't implemented automatic rotation, this
    // signal never fires, so nothing happens — which is the correct no-op.
    const bool portrait = orientation == Qt::PortraitOrientation
        || orientation == Qt::InvertedPortraitOrientation;
    if (portrait) {
        receiver_->setNativePanel(panelShort_, panelLong_, 1.0);
    } else {
        receiver_->setNativePanel(panelLong_, panelShort_, 1.0);
    }
}

void SessionWorker::sendTouch(const QString& phase, const double x, const double y) {
    if (receiver_) {
        receiver_->sendTouch(phase.toStdString(), x, y);
    }
}

void SessionWorker::sendScroll(const double dx, const double dy) {
    if (receiver_) {
        receiver_->sendScroll(dx, dy);
    }
}

void SessionWorker::stop() {
    timer_->stop();
    if (receiver_) {
        receiver_->stop();
        receiver_.reset();
    }
    if (decoder_) {
        decoder_->stop();
        decoder_.reset();
    }
    if (session_) {
        session_->stop();
        session_.reset();
    }
    if (advertiser_) {
        advertiser_.reset();
    }
    receiverConnected_ = false;
    emit stateChanged(QStringLiteral("Disconnected"),
                      QStringLiteral("Ready to connect."), false, false);
}

}  // namespace od::gui
