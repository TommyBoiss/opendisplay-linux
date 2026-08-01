#include "opendisplay/kde_output_controller.hpp"

#include "opendisplay/log.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace od {
namespace {

std::string jsonId(const QJsonValue& value) {
    if (value.isString()) return value.toString().toStdString();
    if (value.isDouble()) return std::to_string(value.toInt());
    return {};
}

Size jsonSize(const QJsonValue& value) {
    const auto object = value.toObject();
    return {.width = object.value(QStringLiteral("width")).toInt(),
            .height = object.value(QStringLiteral("height")).toInt()};
}

bool isQuarterTurn(const int rotation) {
    // KScreen rotations are flags: left=2, inverted=4, right=8; flipped
    // variants add 16. Only left/right exchange the logical axes.
    const int unflipped = rotation & 0x0f;
    return unflipped == 2 || unflipped == 8;
}

bool sameOutput(const DisplayOutput& left, const DisplayOutput& right) {
    return left.id == right.id && left.name == right.name;
}

bool hasMode(const DisplayOutput& output, const Size size, const int refreshRate) {
    return std::any_of(output.modes.begin(), output.modes.end(), [&](const auto& mode) {
        return mode.size.width == size.width && mode.size.height == size.height
            && std::abs(mode.refreshRate - refreshRate) < 0.6;
    });
}

std::string outputList(const std::vector<DisplayOutput>& outputs) {
    std::ostringstream names;
    bool first = true;
    for (const auto& output : outputs) {
        if (!output.connected) continue;
        if (!first) names << ", ";
        names << output.name << " (" << output.id << ')';
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

std::string KdeOutputController::run(const std::vector<std::string>& arguments) {
    QProcess process;
    QStringList converted;
    for (const auto& argument : arguments) converted.append(QString::fromStdString(argument));
    process.start(QStringLiteral("kscreen-doctor"), converted);
    if (!process.waitForStarted(5'000)) {
        throw std::runtime_error(
            "cannot start kscreen-doctor; install the Arch package 'libkscreen'");
    }
    if (!process.waitForFinished(10'000)) {
        process.kill();
        process.waitForFinished();
        throw std::runtime_error("kscreen-doctor timed out");
    }
    const auto standardOutput = process.readAllStandardOutput().toStdString();
    const auto standardError = process.readAllStandardError().trimmed().toStdString();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        throw std::runtime_error("kscreen-doctor failed"
                                 + (standardError.empty() ? std::string{} : ": " + standardError));
    }
    return standardOutput;
}

std::vector<DisplayOutput> KdeOutputController::outputs() const {
    QJsonParseError error;
    const auto bytes = QByteArray::fromStdString(run({"-j"}));
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        throw std::runtime_error("cannot parse kscreen-doctor JSON: "
                                 + error.errorString().toStdString());
    }
    std::vector<DisplayOutput> result;
    for (const auto& value : document.object().value(QStringLiteral("outputs")).toArray()) {
        const auto object = value.toObject();
        DisplayOutput output;
        output.id = jsonId(object.value(QStringLiteral("id")));
        output.name = object.value(QStringLiteral("name")).toString().toStdString();
        output.connected = object.value(QStringLiteral("connected")).toBool();
        output.enabled = object.value(QStringLiteral("enabled")).toBool();
        output.resolution = jsonSize(object.value(QStringLiteral("size")));
        output.scale = object.value(QStringLiteral("scale")).toDouble(1.0);
        const auto position = object.value(QStringLiteral("pos")).toObject();
        output.logicalGeometry.x = position.value(QStringLiteral("x")).toInt();
        output.logicalGeometry.y = position.value(QStringLiteral("y")).toInt();
        if (output.scale > 0) {
            output.logicalGeometry.width = static_cast<int>(
                std::lround(output.resolution.width / output.scale));
            output.logicalGeometry.height = static_cast<int>(
                std::lround(output.resolution.height / output.scale));
        }
        if (isQuarterTurn(object.value(QStringLiteral("rotation")).toInt(1))) {
            std::swap(output.logicalGeometry.width, output.logicalGeometry.height);
        }
        const auto millimetres = jsonSize(object.value(QStringLiteral("sizeMM")));
        if (millimetres.width > 0 && millimetres.height > 0) {
            output.physicalSize = PhysicalSize{
                .widthMm = static_cast<double>(millimetres.width),
                .heightMm = static_cast<double>(millimetres.height),
            };
        }
        for (const auto& modeValue : object.value(QStringLiteral("modes")).toArray()) {
            const auto modeObject = modeValue.toObject();
            output.modes.push_back({
                .id = jsonId(modeObject.value(QStringLiteral("id"))),
                .size = jsonSize(modeObject.value(QStringLiteral("size"))),
                .refreshRate = modeObject.value(QStringLiteral("refreshRate")).toDouble(),
            });
        }
        if (!output.id.empty() && !output.name.empty()) result.push_back(std::move(output));
    }
    return result;
}

DisplayOutput KdeOutputController::waitForAddedOutput(
    const std::vector<DisplayOutput>& before, const std::chrono::milliseconds timeout) const {
    QElapsedTimer timer;
    timer.start();
    std::vector<DisplayOutput> current;
    while (timer.elapsed() < timeout.count()) {
        current = outputs();
        std::vector<DisplayOutput> added;
        std::copy_if(current.begin(), current.end(), std::back_inserter(added),
                     [&](const auto& candidate) {
                         return candidate.connected
                             && std::none_of(before.begin(), before.end(), [&](const auto& old) {
                                    return sameOutput(candidate, old);
                                });
                     });
        if (added.size() == 1) return added.front();
        if (added.size() > 1) {
            throw std::runtime_error(
                "KDE added multiple outputs while the portal was opening; cannot identify the "
                "virtual monitor: " + outputList(added));
        }
        QThread::msleep(100);
    }
    throw std::runtime_error(
        "KDE created a portal stream but its virtual monitor did not appear in kscreen-doctor");
}

bool KdeOutputController::waitForRemovedOutput(
    const DisplayOutput& output, const std::chrono::milliseconds timeout) const {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout.count()) {
        const auto current = outputs();
        if (std::none_of(current.begin(), current.end(), [&](const auto& candidate) {
                return sameOutput(candidate, output);
            })) {
            return true;
        }
        QThread::msleep(100);
    }
    return false;
}

OutputTranslation KdeOutputController::apply(const DisplayOutput& output,
                                             const DisplayLayout& layout) const {
    DisplayOutput current = output;
    if (!hasMode(current, layout.resolution, layout.refreshRate)) {
        const auto command = "output." + output.id + ".addCustomMode."
            + std::to_string(layout.resolution.width) + '.'
            + std::to_string(layout.resolution.height) + '.'
            + std::to_string(layout.refreshRate * 1'000) + ".full";
        try {
            run({command});
        } catch (const std::exception& error) {
            throw std::runtime_error(
                std::string(error.what())
                + "; custom virtual-monitor modes require Plasma/libkscreen 6.6 or newer");
        }

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 3'000) {
            const auto all = outputs();
            const auto found = std::find_if(all.begin(), all.end(), [&](const auto& candidate) {
                return sameOutput(candidate, output);
            });
            if (found != all.end() && hasMode(*found, layout.resolution, layout.refreshRate)) {
                current = *found;
                break;
            }
            QThread::msleep(100);
        }
        if (!hasMode(current, layout.resolution, layout.refreshRate)) {
            throw std::runtime_error("KDE did not expose the requested custom virtual-monitor mode");
        }
    }

    const auto mode = std::to_string(layout.resolution.width) + 'x'
        + std::to_string(layout.resolution.height) + '@'
        + std::to_string(layout.refreshRate);
    auto all = outputs();
    int minimumX = layout.logicalGeometry.x;
    int minimumY = layout.logicalGeometry.y;
    for (const auto& candidate : all) {
        if (candidate.connected && candidate.enabled && !sameOutput(candidate, output)) {
            minimumX = std::min(minimumX, candidate.logicalGeometry.x);
            minimumY = std::min(minimumY, candidate.logicalGeometry.y);
        }
    }
    const int shiftX = std::max(0, -minimumX);
    const int shiftY = std::max(0, -minimumY);
    std::vector<std::string> commands{
        "output." + output.id + ".mode." + mode,
        "output." + output.id + ".scale." + scaleText(layout.scale),
        "output." + output.id + ".position."
            + std::to_string(layout.logicalGeometry.x + shiftX) + ','
            + std::to_string(layout.logicalGeometry.y + shiftY),
    };
    if (shiftX != 0 || shiftY != 0) {
        for (const auto& candidate : all) {
            if (candidate.connected && candidate.enabled && !sameOutput(candidate, output)) {
                commands.push_back("output." + candidate.id + ".position."
                    + std::to_string(candidate.logicalGeometry.x + shiftX) + ','
                    + std::to_string(candidate.logicalGeometry.y + shiftY));
            }
        }
    }
    run(commands);
    log("Configured KDE output " + output.name + " as " + mode + " at scale "
        + scaleText(layout.scale) + ", position "
        + std::to_string(layout.logicalGeometry.x + shiftX) + ','
        + std::to_string(layout.logicalGeometry.y + shiftY));
    return {.x = shiftX, .y = shiftY};
}

void KdeOutputController::restorePositions(
    const std::vector<DisplayOutput>& previousOutputs) const {
    const auto current = outputs();
    std::vector<std::string> commands;
    for (const auto& previous : previousOutputs) {
        const auto found = std::find_if(current.begin(), current.end(), [&](const auto& candidate) {
            return candidate.connected && candidate.enabled && sameOutput(candidate, previous);
        });
        if (found != current.end()) {
            commands.push_back("output." + found->id + ".position."
                + std::to_string(previous.logicalGeometry.x) + ','
                + std::to_string(previous.logicalGeometry.y));
        }
    }
    if (!commands.empty()) run(commands);
}

}  // namespace od
