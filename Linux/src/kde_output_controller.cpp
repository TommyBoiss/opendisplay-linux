#include "opendisplay/kde_output_controller.hpp"

#include "opendisplay/log.hpp"

#include <KScreen/Config>
#include <KScreen/GetConfigOperation>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <KScreen/SetConfigOperation>

#include <QElapsedTimer>
#include <QPoint>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace od {
namespace {

KScreen::ConfigPtr readConfig(const std::string& action) {
    KScreen::GetConfigOperation operation;
    if (!operation.exec() || !operation.config()) {
        throw std::runtime_error("KScreen could not " + action + ": "
                                 + operation.errorString().toStdString());
    }
    return operation.config();
}

void writeConfig(const KScreen::ConfigPtr& config, const std::string& action) {
    KScreen::SetConfigOperation operation(config);
    if (!operation.exec()) {
        throw std::runtime_error("KScreen rejected " + action + ": "
                                 + operation.errorString().toStdString());
    }
}

KScreen::OutputPtr findOutput(const KScreen::ConfigPtr& config,
                              const DisplayOutput& requested) {
    const auto outputs = config->outputs();
    const auto found = std::find_if(outputs.begin(), outputs.end(), [&](const auto& candidate) {
        return candidate->name().toStdString() == requested.name;
    });
    return found == outputs.end() ? KScreen::OutputPtr{} : *found;
}

bool isQuarterTurn(const KScreen::Output::Rotation rotation) {
    return rotation == KScreen::Output::Left || rotation == KScreen::Output::Right
        || rotation == KScreen::Output::Flipped90
        || rotation == KScreen::Output::Flipped270;
}

DisplayOutput displayOutput(const KScreen::OutputPtr& native) {
    DisplayOutput output;
    output.id = std::to_string(native->id());
    output.name = native->name().toStdString();
    output.connected = native->isConnected();
    output.enabled = native->isEnabled();
    output.resolution = {.width = native->size().width(), .height = native->size().height()};
    output.scale = native->scale();
    output.logicalGeometry.x = native->pos().x();
    output.logicalGeometry.y = native->pos().y();
    if (output.scale > 0) {
        output.logicalGeometry.width = static_cast<int>(
            std::lround(output.resolution.width / output.scale));
        output.logicalGeometry.height = static_cast<int>(
            std::lround(output.resolution.height / output.scale));
    }
    if (isQuarterTurn(native->rotation())) {
        std::swap(output.logicalGeometry.width, output.logicalGeometry.height);
    }
    const auto millimetres = native->sizeMm();
    if (millimetres.width() > 0 && millimetres.height() > 0) {
        output.physicalSize = PhysicalSize{
            .widthMm = static_cast<double>(millimetres.width()),
            .heightMm = static_cast<double>(millimetres.height()),
        };
    }
    for (const auto& mode : native->modes()) {
        output.modes.push_back({
            .id = mode->id().toStdString(),
            .size = {.width = mode->size().width(), .height = mode->size().height()},
            .refreshRate = mode->refreshRate(),
        });
    }
    return output;
}

std::vector<DisplayOutput> displayOutputs(const KScreen::ConfigPtr& config) {
    std::vector<DisplayOutput> result;
    for (const auto& output : config->outputs()) result.push_back(displayOutput(output));
    return result;
}

KScreen::ModePtr findMode(const KScreen::OutputPtr& output, const Size size,
                          const int refreshRate) {
    const auto modes = output->modes();
    const auto found = std::find_if(modes.begin(), modes.end(), [&](const auto& mode) {
        return mode->size().width() == size.width && mode->size().height() == size.height
            && std::abs(mode->refreshRate() - static_cast<float>(refreshRate)) < 0.6F;
    });
    return found == modes.end() ? KScreen::ModePtr{} : *found;
}

std::string describeOutput(const KScreen::OutputPtr& output) {
    std::ostringstream description;
    description << output->name().toStdString() << " (session id " << output->id()
                << ", connected " << (output->isConnected() ? "yes" : "no")
                << ", enabled " << (output->isEnabled() ? "yes" : "no")
                << ", current " << output->size().width() << 'x' << output->size().height()
                << ", scale " << output->scale() << ", modes [";
    bool first = true;
    for (const auto& mode : output->modes()) {
        if (!first) description << ", ";
        description << mode->size().width() << 'x' << mode->size().height() << '@'
                    << mode->refreshRate();
        first = false;
    }
    description << ']';
#if OD_KSCREEN_HAS_CUSTOM_MODES
    description << ", custom definitions [";
    first = true;
    for (const auto& mode : output->customModes()) {
        if (!first) description << ", ";
        description << mode.size.width() << 'x' << mode.size.height() << '@'
                    << mode.refreshRate;
        first = false;
    }
    description << ']';
#endif
    description << ')';
    return description.str();
}

std::string outputList(const std::vector<DisplayOutput>& outputs) {
    std::ostringstream names;
    bool first = true;
    for (const auto& output : outputs) {
        if (!output.connected) continue;
        if (!first) names << ", ";
        names << output.name << " (session id " << output.id << ')';
        first = false;
    }
    return names.str();
}

std::string scaleText(const double scale) {
    QString value = QString::number(scale, 'f', 2);
    while (value.endsWith('0')) value.chop(1);
    if (value.endsWith('.')) value.chop(1);
    return value.toStdString();
}

}  // namespace

std::vector<DisplayOutput> KdeOutputController::outputs() const {
    return displayOutputs(readConfig("read outputs"));
}

DisplayOutput KdeOutputController::waitForAddedOutput(
    const std::vector<DisplayOutput>& before, const std::chrono::milliseconds timeout) const {
    QElapsedTimer timer;
    timer.start();
    std::vector<DisplayOutput> current;
    while (timer.elapsed() < timeout.count()) {
        current = outputs();
        const auto added = addedDisplayOutputs(before, current);
        if (added.size() == 1) return added.front();
        if (added.size() > 1) {
            throw std::runtime_error(
                "KDE added multiple outputs while the portal was opening; cannot identify the "
                "virtual monitor: " + outputList(added));
        }
        QThread::msleep(100);
    }
    throw std::runtime_error(
        "KDE created a portal stream but its virtual monitor did not appear in KScreen; "
        "last snapshot: " + outputList(current));
}

bool KdeOutputController::waitForRemovedOutput(
    const DisplayOutput& output, const std::chrono::milliseconds timeout) const {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout.count()) {
        const auto current = outputs();
        if (std::none_of(current.begin(), current.end(), [&](const auto& candidate) {
                return sameDisplayOutput(candidate, output);
            })) {
            return true;
        }
        QThread::msleep(100);
    }
    return false;
}

OutputTranslation KdeOutputController::apply(const DisplayOutput& output,
                                             const DisplayLayout& layout) const {
    auto config = readConfig("prepare the virtual output");
    auto current = findOutput(config, output);
    if (!current) {
        throw std::runtime_error("KDE virtual output '" + output.name
                                 + "' disappeared before it could be configured");
    }
    debug("KScreen before configuration: " + describeOutput(current));

    auto mode = findMode(current, layout.resolution, layout.refreshRate);
    if (!mode) {
#if OD_KSCREEN_HAS_CUSTOM_MODES
        if (!(current->capabilities() & KScreen::Output::Capability::CustomModes)) {
            throw std::runtime_error("KDE output '" + output.name
                                     + "' does not advertise custom-mode support; "
                                     + describeOutput(current));
        }
        auto customModes = current->customModes();
        const bool alreadyRequested = std::any_of(
            customModes.begin(), customModes.end(), [&](const auto& candidate) {
                return candidate.size.width() == layout.resolution.width
                    && candidate.size.height() == layout.resolution.height
                    && std::abs(candidate.refreshRate
                                - static_cast<float>(layout.refreshRate)) < 0.6F;
            });
        if (!alreadyRequested) {
            customModes.push_back(KScreen::ModeInfo{
                .size = QSize(layout.resolution.width, layout.resolution.height),
                .refreshRate = static_cast<float>(layout.refreshRate),
                .flags = KScreen::ModeInfo::Flag::Custom,
            });
            current->setCustomModes(customModes);
        }
        debug("Requesting KScreen custom mode " + std::to_string(layout.resolution.width) + 'x'
              + std::to_string(layout.resolution.height) + '@'
              + std::to_string(layout.refreshRate) + " on " + output.name);
        writeConfig(config, "custom mode " + std::to_string(layout.resolution.width) + 'x'
                              + std::to_string(layout.resolution.height) + '@'
                              + std::to_string(layout.refreshRate) + " for " + output.name);

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 3'000) {
            config = readConfig("verify the custom mode");
            current = findOutput(config, output);
            if (!current) {
                throw std::runtime_error("KDE virtual output '" + output.name
                                         + "' disappeared while adding its custom mode");
            }
            mode = findMode(current, layout.resolution, layout.refreshRate);
            if (mode) break;
            QThread::msleep(100);
        }
        if (!mode) {
            throw std::runtime_error(
                "KScreen " OD_KSCREEN_VERSION_STRING " applied the custom-mode request, but KWin "
                "did not announce " + std::to_string(layout.resolution.width) + 'x'
                + std::to_string(layout.resolution.height) + '@'
                + std::to_string(layout.refreshRate) + " within 3 seconds; "
                + describeOutput(current));
        }
#else
        throw std::runtime_error(
            "KScreen " OD_KSCREEN_VERSION_STRING " cannot add custom modes; version 6.6 or newer "
            "is required for " + std::to_string(layout.resolution.width) + 'x'
            + std::to_string(layout.resolution.height) + '@'
            + std::to_string(layout.refreshRate));
#endif
    }

    config = readConfig("prepare the final layout");
    current = findOutput(config, output);
    if (!current) {
        throw std::runtime_error("KDE virtual output '" + output.name
                                 + "' disappeared before layout was applied");
    }
    mode = findMode(current, layout.resolution, layout.refreshRate);
    if (!mode) {
        throw std::runtime_error("KDE lost the requested mode before layout application; "
                                 + describeOutput(current));
    }

    const auto all = displayOutputs(config);
    int minimumX = layout.logicalGeometry.x;
    int minimumY = layout.logicalGeometry.y;
    for (const auto& candidate : all) {
        if (candidate.connected && candidate.enabled
            && !sameDisplayOutput(candidate, output)) {
            minimumX = std::min(minimumX, candidate.logicalGeometry.x);
            minimumY = std::min(minimumY, candidate.logicalGeometry.y);
        }
    }
    const int shiftX = std::max(0, -minimumX);
    const int shiftY = std::max(0, -minimumY);
    current->setCurrentModeId(mode->id());
    current->setScale(layout.scale);
    current->setPos(QPoint(layout.logicalGeometry.x + shiftX,
                           layout.logicalGeometry.y + shiftY));
    if (shiftX != 0 || shiftY != 0) {
        for (const auto& candidate : all) {
            if (!candidate.connected || !candidate.enabled
                || sameDisplayOutput(candidate, output)) {
                continue;
            }
            const auto native = findOutput(config, candidate);
            if (native) {
                native->setPos(QPoint(candidate.logicalGeometry.x + shiftX,
                                      candidate.logicalGeometry.y + shiftY));
            }
        }
    }
    writeConfig(config, "mode, scale, and position for " + output.name);

    const auto verifiedConfig = readConfig("verify the final layout");
    const auto verified = findOutput(verifiedConfig, output);
    if (!verified) {
        throw std::runtime_error("KDE virtual output '" + output.name
                                 + "' disappeared after layout application");
    }
    const auto activeMode = verified->currentMode();
    if (!activeMode || activeMode->size().width() != layout.resolution.width
        || activeMode->size().height() != layout.resolution.height
        || std::abs(activeMode->refreshRate()
                    - static_cast<float>(layout.refreshRate)) >= 0.6F) {
        throw std::runtime_error("KDE did not activate the requested virtual mode; "
                                 + describeOutput(verified));
    }
    debug("KScreen after configuration: " + describeOutput(verified));
    log("Configured KDE output " + output.name + " as "
        + std::to_string(layout.resolution.width) + 'x'
        + std::to_string(layout.resolution.height) + '@'
        + std::to_string(layout.refreshRate) + " at scale " + scaleText(layout.scale)
        + ", position " + std::to_string(layout.logicalGeometry.x + shiftX) + ','
        + std::to_string(layout.logicalGeometry.y + shiftY));
    return {.x = shiftX, .y = shiftY};
}

void KdeOutputController::restorePositions(
    const std::vector<DisplayOutput>& previousOutputs) const {
    auto config = readConfig("prepare output-position restoration");
    bool changed = false;
    for (const auto& previous : previousOutputs) {
        const auto current = findOutput(config, previous);
        if (current && current->isConnected() && current->isEnabled()) {
            current->setPos(QPoint(previous.logicalGeometry.x, previous.logicalGeometry.y));
            changed = true;
        }
    }
    if (changed) writeConfig(config, "restored physical-output positions");
}

}  // namespace od
