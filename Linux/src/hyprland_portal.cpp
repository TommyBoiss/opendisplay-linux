#include "opendisplay/hyprland_portal.hpp"

#include "opendisplay/log.hpp"

#include <QCoreApplication>
#include <QDBusObjectPath>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace od {
namespace {

constexpr auto screenCastInterface = "org.freedesktop.portal.ScreenCast";

}  // namespace

HyprlandPortal::HyprlandPortal(QObject* parent) : QObject(parent), portal_(this) {}

HyprlandPortal::~HyprlandPortal() { stop(); }

PortalCapture HyprlandPortal::start(const DesktopRequest& request) {
    stop();
    const int requestedWidth = std::max(2, request.receiver.pixelsWide);
    const int requestedHeight = std::max(2, request.receiver.pixelsHigh);
    int captureWidth = requestedWidth;
    int captureHeight = requestedHeight;
    int captureLogicalWidth = requestedWidth;
    int captureLogicalHeight = requestedHeight;
    std::string inputOutputName;

    try {
        const auto currentOutputs = outputs_.outputs();
        const auto reference = selectReferenceOutput(currentOutputs,
                                                     request.display.referenceMonitor);
        inputOutputName = reference.name;
        if (request.mode == CaptureMode::Extend) {
            const auto layout = planDisplayLayout(reference, request.receiver, request.display,
                                                  request.refreshRate,
                                                  DisplayModePolicy::IntegerLogicalSize);
            log("Reference monitor: " + reference.name + " "
                + std::to_string(layout.reference.resolution.width) + 'x'
                + std::to_string(layout.reference.resolution.height) + " at scale "
                + std::to_string(layout.reference.scale));
            if (layout.usedPhysicalSizing) {
                debug("Virtual scale derived from reference and receiver physical sizes");
            } else if (!request.display.virtualScale) {
                debug("Virtual scale derived from the receiver native scale");
            }
            if (layout.adjustedResolution) {
                debug("Adjusted virtual resolution for integer logical geometry: "
                      + std::to_string(layout.resolution.width) + 'x'
                      + std::to_string(layout.resolution.height));
            }
            virtualOutputName_ = "OpenDisplay-"
                + std::to_string(QCoreApplication::applicationPid());
            outputCreated_ = true;
            outputs_.create(virtualOutputName_);
            const auto configured = outputs_.configure(virtualOutputName_, layout);
            inputOutputName = configured.name;
            captureWidth = layout.resolution.width;
            captureHeight = layout.resolution.height;
            captureLogicalWidth = layout.logicalGeometry.width;
            captureLogicalHeight = layout.logicalGeometry.height;
            log("Hyprland virtual monitor ready: " + virtualOutputName_ + " "
                + std::to_string(layout.resolution.width) + 'x'
                + std::to_string(layout.resolution.height) + '@'
                + std::to_string(layout.refreshRate) + ", scale "
                + std::to_string(layout.scale));
            log("In the screen-sharing dialog, select monitor " + virtualOutputName_ + '.');
        } else {
            log("In the screen-sharing dialog, select monitor " + reference.name + '.');
            captureWidth = reference.resolution.width;
            captureHeight = reference.resolution.height;
            captureLogicalWidth = reference.logicalGeometry.width;
            captureLogicalHeight = reference.logicalGeometry.height;
        }

        const auto availableSources = portal_.property(screenCastInterface,
                                                       "AvailableSourceTypes");
        if (availableSources.isValid() && (availableSources.toUInt() & 1U) == 0) {
            throw std::runtime_error(
                "the active Hyprland portal does not advertise monitor capture");
        }
        const auto availableCursorModes = portal_.property(screenCastInterface,
                                                           "AvailableCursorModes");
        log("Requesting a Hyprland screen-cast portal session…");
        sessionPath_ = portal_.createSession(screenCastInterface);

        QVariantMap sourceOptions;
        sourceOptions.insert(QStringLiteral("types"), 1U);  // MONITOR
        sourceOptions.insert(QStringLiteral("multiple"), false);
        const bool canEmbedCursor = !availableCursorModes.isValid()
            || (availableCursorModes.toUInt() & 2U) != 0;
        sourceOptions.insert(QStringLiteral("cursor_mode"), canEmbedCursor ? 2U : 1U);
        portal_.request(screenCastInterface, QStringLiteral("SelectSources"),
                        {QVariant::fromValue(QDBusObjectPath(sessionPath_))},
                        std::move(sourceOptions));
        const auto started = portal_.request(
            screenCastInterface, QStringLiteral("Start"),
            {QVariant::fromValue(QDBusObjectPath(sessionPath_)), QString()}, {});
        const auto parsed = XdgPortal::firstStream(
            started.value(QStringLiteral("streams")),
            std::max(1, captureWidth), std::max(1, captureHeight));
        if (!parsed) {
            throw std::runtime_error("portal did not return a PipeWire stream");
        }
        auto stream = *parsed;
        if (request.mode == CaptureMode::Extend) {
            stream.logicalWidth = captureLogicalWidth;
            stream.logicalHeight = captureLogicalHeight;
        }
        if (request.requestInput) {
            pointer_.start(inputOutputName);
            inputEnabled_ = true;
        }
        const int fd = portal_.openPipeWireRemote(sessionPath_);
        log("Hyprland portal session ready; PipeWire node "
            + std::to_string(stream.nodeId) + ", portal size "
            + std::to_string(stream.logicalWidth) + 'x'
            + std::to_string(stream.logicalHeight));
        return {
            .sessionPath = sessionPath_.toStdString(),
            .stream = stream,
            .pipewireFd = fd,
            .captureWidth = captureWidth,
            .captureHeight = captureHeight,
        };
    } catch (...) {
        stop();
        throw;
    }
}

void HyprlandPortal::stop() {
    inputEnabled_ = false;
    pointer_.stop();
    if (!sessionPath_.isEmpty()) {
        portal_.closeSession(sessionPath_);
        sessionPath_.clear();
    }
    if (outputCreated_) {
        try {
            outputs_.remove(virtualOutputName_);
        } catch (const std::exception& error) {
            debug(std::string("Cannot remove Hyprland virtual output ")
                  + virtualOutputName_ + ": " + error.what());
        }
    }
    outputCreated_ = false;
    virtualOutputName_.clear();
}

void HyprlandPortal::pointer(const std::string_view phase, const double normalizedX,
                             const double normalizedY) {
    if (inputEnabled_) pointer_.pointer(phase, normalizedX, normalizedY);
}

void HyprlandPortal::scroll(const double dx, const double dy) {
    if (inputEnabled_) pointer_.scroll(dx, dy);
}

}  // namespace od
