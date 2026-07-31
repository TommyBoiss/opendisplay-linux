#include "opendisplay/kde_portal.hpp"

#include "opendisplay/log.hpp"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QEventLoop>
#include <QRandomGenerator>
#include <QTimer>
#include <QVariant>

#include <linux/input-event-codes.h>
#include <unistd.h>

#include <algorithm>
#include <stdexcept>

namespace od {
namespace {

constexpr auto portalService = "org.freedesktop.portal.Desktop";
constexpr auto portalPath = "/org/freedesktop/portal/desktop";
constexpr auto requestInterface = "org.freedesktop.portal.Request";
constexpr auto sessionInterface = "org.freedesktop.portal.Session";
constexpr auto screenCastInterface = "org.freedesktop.portal.ScreenCast";
constexpr auto remoteDesktopInterface = "org.freedesktop.portal.RemoteDesktop";

QString token(const QString& prefix) {
    return prefix + QString::number(QRandomGenerator::global()->generate64(), 16);
}

QString requestPath(const QDBusConnection& bus, const QString& handleToken) {
    auto sender = bus.baseService();
    sender.remove(0, 1);
    sender.replace('.', '_');
    return QStringLiteral("/org/freedesktop/portal/desktop/request/") + sender + '/'
        + handleToken;
}

QVariantMap withToken(QVariantMap options, const QString& handleToken) {
    options.insert(QStringLiteral("handle_token"), handleToken);
    return options;
}

}  // namespace

KdePortal::KdePortal(QObject* parent)
    : QObject(parent), bus_(QDBusConnection::sessionBus()) {
    if (!bus_.isConnected()) {
        throw std::runtime_error("cannot connect to the D-Bus session bus");
    }
}

KdePortal::~KdePortal() { stop(); }

QVariant KdePortal::unwrap(QVariant value) {
    while (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }
    return value;
}

QVariantMap KdePortal::request(const QString& interface, const QString& method,
                               const QVariantList& arguments, QVariantMap options) {
    if (waiting_) {
        throw std::runtime_error("a portal request is already pending");
    }
    const auto handleToken = token(QStringLiteral("od"));
    const auto expectedPath = requestPath(bus_, handleToken);
    options = withToken(std::move(options), handleToken);

    if (!bus_.connect(portalService, expectedPath, requestInterface, QStringLiteral("Response"),
                      this, SLOT(requestResponse(uint,QVariantMap)))) {
        throw std::runtime_error("cannot subscribe to the portal response");
    }

    waiting_ = true;
    responseCode_ = 2;
    responseResults_.clear();

    auto message = QDBusMessage::createMethodCall(portalService, portalPath, interface, method);
    auto callArguments = arguments;
    callArguments.append(options);
    message.setArguments(callArguments);
    const auto reply = bus_.call(message, QDBus::Block, 30'000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        waiting_ = false;
        bus_.disconnect(portalService, expectedPath, requestInterface, QStringLiteral("Response"),
                        this, SLOT(requestResponse(uint,QVariantMap)));
        throw std::runtime_error((method + QStringLiteral(" failed: ") + reply.errorMessage())
                                     .toStdString());
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
    QTimer poll;
    connect(&poll, &QTimer::timeout, &loop, [&] {
        if (!waiting_) {
            loop.quit();
        }
    });
    timeout.start(300'000);
    poll.start(10);
    loop.exec();
    waiting_ = false;
    bus_.disconnect(portalService, expectedPath, requestInterface, QStringLiteral("Response"),
                    this, SLOT(requestResponse(uint,QVariantMap)));
    if (!timeout.isActive()) {
        throw std::runtime_error("portal request timed out");
    }
    if (responseCode_ != 0) {
        throw std::runtime_error(responseCode_ == 1 ? "portal request cancelled"
                                                    : "portal request denied");
    }
    return responseResults_;
}

void KdePortal::requestResponse(const uint response, const QVariantMap& results) {
    responseCode_ = response;
    responseResults_ = results;
    waiting_ = false;
}

std::optional<PortalStream> KdePortal::firstStream(const QVariant& raw, const int fallbackWidth,
                                                   const int fallbackHeight) {
    auto value = unwrap(raw);
    if (!value.canConvert<QDBusArgument>()) {
        return std::nullopt;
    }
    // Portal replies are unmarshalled as read-only QDBusArgument objects.
    // Keeping this copy const is significant: Qt overloads beginArray() and
    // beginStructure() for read vs. write mode based on constness.
    const auto argument = value.value<QDBusArgument>();
    debug("Portal streams D-Bus signature: " + argument.currentSignature().toStdString());
    if (argument.currentType() != QDBusArgument::ArrayType) {
        return std::nullopt;
    }
    argument.beginArray();
    if (argument.atEnd()) {
        argument.endArray();
        return std::nullopt;
    }
    if (argument.currentType() != QDBusArgument::StructureType) {
        argument.endArray();
        return std::nullopt;
    }
    PortalStream stream;
    QVariantMap properties;
    argument.beginStructure();
    if (argument.currentType() != QDBusArgument::BasicType) {
        argument.endStructure();
        argument.endArray();
        return std::nullopt;
    }
    argument >> stream.nodeId;
    if (argument.currentType() != QDBusArgument::MapType) {
        argument.endStructure();
        argument.endArray();
        return std::nullopt;
    }
    argument >> properties;
    argument.endStructure();
    argument.endArray();
    stream.logicalWidth = fallbackWidth;
    stream.logicalHeight = fallbackHeight;

    const auto sizeValue = unwrap(properties.value(QStringLiteral("size")));
    if (sizeValue.canConvert<QDBusArgument>()) {
        const auto size = sizeValue.value<QDBusArgument>();
        if (size.currentType() == QDBusArgument::StructureType) {
            size.beginStructure();
            size >> stream.logicalWidth >> stream.logicalHeight;
            size.endStructure();
        }
    }
    return stream;
}

PortalCapture KdePortal::start(const CaptureMode mode, const int requestedWidth,
                               const int requestedHeight, const bool requestInput) {
    stop();
    QDBusInterface screenCast(portalService, portalPath, screenCastInterface, bus_);
    const auto availableSources = screenCast.property("AvailableSourceTypes");
    const auto availableCursorModes = screenCast.property("AvailableCursorModes");
    if (mode == CaptureMode::Extend && availableSources.isValid()
        && (availableSources.toUInt() & 4U) == 0) {
        throw std::runtime_error(
            "the active KDE portal does not advertise virtual-monitor capture; use --mode mirror");
    }
    if (requestInput) {
        QDBusInterface remoteDesktop(portalService, portalPath, remoteDesktopInterface, bus_);
        const auto availableDevices = remoteDesktop.property("AvailableDeviceTypes");
        if (availableDevices.isValid() && (availableDevices.toUInt() & 2U) == 0) {
            throw std::runtime_error(
                "the active KDE portal does not advertise pointer control; use --no-input");
        }
    }
    log("Requesting a KDE remote-desktop portal session…");

    QVariantMap createOptions;
    createOptions.insert(QStringLiteral("session_handle_token"), token(QStringLiteral("session")));
    const auto createResults = request(remoteDesktopInterface, QStringLiteral("CreateSession"), {},
                                       std::move(createOptions));
    const auto sessionValue = unwrap(createResults.value(QStringLiteral("session_handle")));
    if (sessionValue.canConvert<QDBusObjectPath>()) {
        sessionPath_ = sessionValue.value<QDBusObjectPath>().path();
    } else {
        sessionPath_ = sessionValue.toString();
    }
    if (sessionPath_.isEmpty()) {
        throw std::runtime_error("portal did not return a session handle");
    }

    if (requestInput) {
        QVariantMap deviceOptions;
        deviceOptions.insert(QStringLiteral("types"), 2U);  // POINTER
        request(remoteDesktopInterface, QStringLiteral("SelectDevices"),
                {QVariant::fromValue(QDBusObjectPath(sessionPath_))}, std::move(deviceOptions));
    }

    QVariantMap sourceOptions;
    sourceOptions.insert(QStringLiteral("types"), mode == CaptureMode::Extend ? 4U : 1U);
    sourceOptions.insert(QStringLiteral("multiple"), false);
    // The CLI does not yet transmit cursor sprites, so prefer embedding the
    // cursor in video and fall back to the universally available hidden mode.
    const bool canEmbedCursor = !availableCursorModes.isValid()
        || (availableCursorModes.toUInt() & 2U) != 0;
    sourceOptions.insert(QStringLiteral("cursor_mode"), canEmbedCursor ? 2U : 1U);
    request(screenCastInterface, QStringLiteral("SelectSources"),
            {QVariant::fromValue(QDBusObjectPath(sessionPath_))}, std::move(sourceOptions));

    const auto started = request(remoteDesktopInterface, QStringLiteral("Start"),
                                 {QVariant::fromValue(QDBusObjectPath(sessionPath_)), QString()}, {});
    const auto parsed = firstStream(started.value(QStringLiteral("streams")),
                                    std::max(1, requestedWidth / 2),
                                    std::max(1, requestedHeight / 2));
    if (!parsed) {
        stop();
        throw std::runtime_error("portal did not return a PipeWire stream");
    }
    stream_ = *parsed;
    inputEnabled_ = requestInput;

    auto open = QDBusMessage::createMethodCall(portalService, portalPath, screenCastInterface,
                                                QStringLiteral("OpenPipeWireRemote"));
    open.setArguments({QVariant::fromValue(QDBusObjectPath(sessionPath_)), QVariantMap{}});
    const auto openReply = bus_.call(open, QDBus::Block, 30'000);
    if (openReply.type() == QDBusMessage::ErrorMessage || openReply.arguments().isEmpty()) {
        stop();
        throw std::runtime_error("portal did not provide a PipeWire remote");
    }
    const auto descriptor = openReply.arguments().front().value<QDBusUnixFileDescriptor>();
    const int fd = ::dup(descriptor.fileDescriptor());
    if (fd < 0) {
        stop();
        throw std::runtime_error("cannot duplicate PipeWire portal descriptor");
    }
    log("KDE portal session ready; PipeWire node " + std::to_string(stream_.nodeId)
        + ", portal size " + std::to_string(stream_.logicalWidth) + "x"
        + std::to_string(stream_.logicalHeight));
    return PortalCapture{.sessionPath = sessionPath_.toStdString(), .stream = stream_,
                         .pipewireFd = fd};
}

void KdePortal::stop() {
    if (sessionPath_.isEmpty()) {
        return;
    }
    auto close = QDBusMessage::createMethodCall(portalService, sessionPath_, sessionInterface,
                                                QStringLiteral("Close"));
    bus_.call(close, QDBus::NoBlock);
    sessionPath_.clear();
    stream_ = {};
    inputEnabled_ = false;
    pointerDown_ = false;
}

void KdePortal::notify(const QString& method, const QVariantList& arguments) {
    if (!inputEnabled_ || sessionPath_.isEmpty()) {
        return;
    }
    auto message = QDBusMessage::createMethodCall(portalService, portalPath,
                                                   remoteDesktopInterface, method);
    QVariantList full{QVariant::fromValue(QDBusObjectPath(sessionPath_)), QVariantMap{}};
    full.append(arguments);
    message.setArguments(full);
    bus_.call(message, QDBus::NoBlock);
}

void KdePortal::pointer(const std::string_view phase, const double normalizedX,
                        const double normalizedY) {
    const double x = std::clamp(normalizedX, 0.0, 1.0) * stream_.logicalWidth;
    const double y = std::clamp(normalizedY, 0.0, 1.0) * stream_.logicalHeight;
    notify(QStringLiteral("NotifyPointerMotionAbsolute"), {stream_.nodeId, x, y});
    if (phase == "began") {
        pointerDown_ = true;
        notify(QStringLiteral("NotifyPointerButton"), {BTN_LEFT, 1U});
    } else if ((phase == "ended" || phase == "cancelled") && pointerDown_) {
        pointerDown_ = false;
        notify(QStringLiteral("NotifyPointerButton"), {BTN_LEFT, 0U});
    }
}

void KdePortal::scroll(const double dx, const double dy) {
    notify(QStringLiteral("NotifyPointerAxis"), {dx, dy});
}

}  // namespace od
