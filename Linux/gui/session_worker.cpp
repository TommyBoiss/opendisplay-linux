#include "session_worker.hpp"

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
    if (session_) return;

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

void SessionWorker::tick() {
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

void SessionWorker::stop() {
    timer_->stop();
    if (!session_) return;
    session_->stop();
    session_.reset();
    emit stateChanged(QStringLiteral("Disconnected"),
                      QStringLiteral("Ready to connect."), false, false);
}

}  // namespace od::gui
