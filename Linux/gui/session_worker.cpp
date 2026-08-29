#include "session_worker.hpp"

#include "opendisplay/avahi_publish.hpp"
#include "opendisplay/desktop_backend_factory.hpp"
#include "opendisplay/discovery.hpp"

#include <QTimer>

#include <chrono>
#include <exception>
#include <string>

namespace od::gui {

SessionWorker::SessionWorker(QObject* parent) : QObject(parent), timer_(new QTimer(this)) {
    timer_->setInterval(std::chrono::milliseconds(20));
    connect(timer_, &QTimer::timeout, this, &SessionWorker::tick);
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
    emit stateChanged(QStringLiteral("Listening"),
                      QStringLiteral("Waiting for a sender on port %1…").arg(options.port),
                      false, true);
    try {
        od::PhoneInfo panel{.pixelsWide = 1920, .pixelsHigh = 1080, .scale = 1.0,
                            .device = "Linux", .installId = "linux-receiver",
                            .protocolVersion = 2};
        receiver_ = std::make_unique<od::ReceiverSession>();
        decoder_ = std::make_unique<od::FfmpegDecoder>();
        // usbmuxd protocol (senders reach us with USBMUXD_SOCKET_ADDRESS) or
        // plain TCP, depending on the selected transport.
        if (options.transport == od::TransportKind::Usb) {
            receiver_->startUsbmux(options.port, panel,
                                   [this](const od::ReceivedFrame& frame) {
                                       decoder_->submit(frame.bgra);
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
                            [this](const std::string& reason) {
                                emit stateChanged(QStringLiteral("Disconnected"),
                                                  QString::fromStdString(reason),
                                                  false, false);
                            });
            // Advertise over mDNS so senders can discover us.
            const std::string publishError = od::publishReceiver(
                options.serviceName, options.port, "linux-receiver");
            if (!publishError.empty()) {
                emit stateChanged(QStringLiteral("Connection failed"),
                                  QString::fromStdString(publishError), false, false);
                return;
            }
        }
        decoder_->start([this](const od::DecodedFrame& frame) {
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
            if (receiver_->tick()) return;
            timer_->stop();
            receiver_->stop();
            receiver_.reset();
            if (decoder_) decoder_->stop();
            decoder_.reset();
            emit stateChanged(QStringLiteral("Disconnected"),
                              QStringLiteral("The sender ended the connection."), false, false);
        } catch (const std::exception& error) {
            timer_->stop();
            receiver_->stop();
            receiver_.reset();
            if (decoder_) decoder_->stop();
            decoder_.reset();
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
    emit stateChanged(QStringLiteral("Disconnected"),
                      QStringLiteral("Ready to connect."), false, false);
}

}  // namespace od::gui
