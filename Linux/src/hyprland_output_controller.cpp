#include "opendisplay/hyprland_output_controller.hpp"

#include "opendisplay/log.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace od {
namespace {

struct CommandResult {
    bool success = false;
    std::string output;
};

CommandResult hyprctl(const QStringList& arguments) {
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral("hyprctl"), arguments);
    if (!process.waitForStarted(3'000)) {
        return {.success = false, .output = "cannot start hyprctl: "
                    + process.errorString().toStdString()};
    }
    if (!process.waitForFinished(5'000)) {
        process.kill();
        process.waitForFinished();
        return {.success = false, .output = "hyprctl timed out"};
    }
    return {
        .success = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
        .output = process.readAll().trimmed().toStdString(),
    };
}

std::string outputSnapshot(const std::vector<DisplayOutput>& outputs) {
    std::ostringstream result;
    bool first = true;
    for (const auto& output : outputs) {
        if (!first) result << ", ";
        result << output.name << ' ' << output.resolution.width << 'x'
               << output.resolution.height << '+' << output.logicalGeometry.x << '+'
               << output.logicalGeometry.y << " scale " << output.scale;
        first = false;
    }
    return result.str();
}

std::string scaleText(const double scale) {
    QString text = QString::number(scale, 'f', 4);
    while (text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text.toStdString();
}

std::optional<DisplayOutput> findOutput(const std::vector<DisplayOutput>& outputs,
                                        const std::string& name) {
    const auto found = std::find_if(outputs.begin(), outputs.end(), [&](const auto& output) {
        return output.name == name;
    });
    return found == outputs.end() ? std::nullopt : std::optional(*found);
}

}  // namespace

std::vector<DisplayOutput> parseHyprlandOutputs(const std::string_view json) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(
        QByteArray(json.data(), static_cast<qsizetype>(json.size())), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        throw std::runtime_error("hyprctl returned invalid monitor JSON: "
                                 + error.errorString().toStdString());
    }
    std::vector<DisplayOutput> outputs;
    static const QRegularExpression modePattern(
        QStringLiteral(R"(^(\d+)x(\d+)@([0-9.]+)Hz$)"));
    for (const auto& value : document.array()) {
        const auto native = value.toObject();
        DisplayOutput output;
        output.id = QString::number(native.value(QStringLiteral("id")).toInt()).toStdString();
        output.name = native.value(QStringLiteral("name")).toString().toStdString();
        output.resolution = {
            .width = native.value(QStringLiteral("width")).toInt(),
            .height = native.value(QStringLiteral("height")).toInt(),
        };
        output.connected = !output.name.empty();
        output.enabled = !native.value(QStringLiteral("disabled")).toBool()
            && output.resolution.width > 0 && output.resolution.height > 0;
        output.scale = native.value(QStringLiteral("scale")).toDouble(1.0);
        if (output.scale <= 0) output.scale = 1.0;
        const double physicalWidth = native.value(QStringLiteral("physicalWidth")).toDouble();
        const double physicalHeight = native.value(QStringLiteral("physicalHeight")).toDouble();
        if (physicalWidth > 0 && physicalHeight > 0) {
            output.physicalSize = PhysicalSize{
                .widthMm = physicalWidth,
                .heightMm = physicalHeight,
            };
        }
        output.logicalGeometry = {
            .x = native.value(QStringLiteral("x")).toInt(),
            .y = native.value(QStringLiteral("y")).toInt(),
            .width = static_cast<int>(std::lround(output.resolution.width / output.scale)),
            .height = static_cast<int>(std::lround(output.resolution.height / output.scale)),
        };
        const int transform = native.value(QStringLiteral("transform")).toInt();
        if (transform == 1 || transform == 3 || transform == 5 || transform == 7) {
            std::swap(output.logicalGeometry.width, output.logicalGeometry.height);
        }
        for (const auto& modeValue : native.value(QStringLiteral("availableModes")).toArray()) {
            const auto match = modePattern.match(modeValue.toString());
            if (!match.hasMatch()) continue;
            output.modes.push_back({
                .id = modeValue.toString().toStdString(),
                .size = {.width = match.captured(1).toInt(),
                         .height = match.captured(2).toInt()},
                .refreshRate = match.captured(3).toDouble(),
            });
        }
        outputs.push_back(std::move(output));
    }
    return outputs;
}

std::string hyprlandMonitorExpression(const std::string& outputName,
                                      const DisplayLayout& layout) {
    const auto& geometry = layout.logicalGeometry;
    std::ostringstream expression;
    expression << "hl.monitor({ output = \"" << outputName << "\", mode = \""
               << layout.resolution.width << 'x' << layout.resolution.height << '@'
               << layout.refreshRate << "\", position = \"" << geometry.x << 'x'
               << geometry.y << "\", scale = " << scaleText(layout.scale) << " })";
    return expression.str();
}

std::vector<DisplayOutput> HyprlandOutputController::outputs() const {
    const auto result = hyprctl({QStringLiteral("-j"), QStringLiteral("monitors"),
                                 QStringLiteral("all")});
    if (!result.success) {
        throw std::runtime_error("cannot query Hyprland monitors with hyprctl: "
                                 + result.output);
    }
    return parseHyprlandOutputs(result.output);
}

void HyprlandOutputController::create(const std::string& outputName) const {
    if (findOutput(outputs(), outputName)) {
        throw std::runtime_error("Hyprland output '" + outputName + "' already exists");
    }
    const auto result = hyprctl({QStringLiteral("output"), QStringLiteral("create"),
                                 QStringLiteral("headless"),
                                 QString::fromStdString(outputName)});
    if (!result.success) {
        throw std::runtime_error("Hyprland could not create headless output '" + outputName
                                 + "': " + result.output);
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3'000) {
        if (findOutput(outputs(), outputName)) return;
        QThread::msleep(100);
    }
    throw std::runtime_error("Hyprland accepted the headless-output request, but '"
                             + outputName + "' did not appear within 3 seconds");
}

DisplayOutput HyprlandOutputController::configure(const std::string& outputName,
                                                  const DisplayLayout& layout) const {
    const auto expression = hyprlandMonitorExpression(outputName, layout);
    auto result = hyprctl({QStringLiteral("eval"), QString::fromStdString(expression)});
    if (!result.success) {
        debug("Hyprland Lua monitor command failed; trying the pre-0.55 keyword syntax: "
              + result.output);
        const auto& geometry = layout.logicalGeometry;
        const QString legacy = QStringLiteral("%1,%2x%3@%4,%5x%6,%7")
            .arg(QString::fromStdString(outputName))
            .arg(layout.resolution.width)
            .arg(layout.resolution.height)
            .arg(layout.refreshRate)
            .arg(geometry.x)
            .arg(geometry.y)
            .arg(QString::fromStdString(scaleText(layout.scale)));
        result = hyprctl({QStringLiteral("keyword"), QStringLiteral("monitor"), legacy});
    }
    if (!result.success) {
        throw std::runtime_error("Hyprland rejected virtual monitor mode "
                                 + std::to_string(layout.resolution.width) + 'x'
                                 + std::to_string(layout.resolution.height) + '@'
                                 + std::to_string(layout.refreshRate) + " scale "
                                 + scaleText(layout.scale) + ": " + result.output);
    }

    QElapsedTimer timer;
    timer.start();
    std::vector<DisplayOutput> current;
    while (timer.elapsed() < 3'000) {
        current = outputs();
        const auto output = findOutput(current, outputName);
        if (output && output->resolution.width == layout.resolution.width
            && output->resolution.height == layout.resolution.height
            && output->logicalGeometry.x == layout.logicalGeometry.x
            && output->logicalGeometry.y == layout.logicalGeometry.y
            && std::abs(output->scale - layout.scale) < 0.01) {
            return *output;
        }
        QThread::msleep(100);
    }
    throw std::runtime_error("Hyprland accepted the virtual-monitor command but did not apply "
                             "the requested mode; last monitor snapshot: "
                             + outputSnapshot(current));
}

void HyprlandOutputController::remove(const std::string& outputName) const {
    if (!findOutput(outputs(), outputName)) return;
    const auto result = hyprctl({QStringLiteral("output"), QStringLiteral("remove"),
                                 QString::fromStdString(outputName)});
    if (!result.success) {
        throw std::runtime_error("Hyprland could not remove output '" + outputName
                                 + "': " + result.output);
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3'000) {
        if (!findOutput(outputs(), outputName)) return;
        QThread::msleep(100);
    }
    throw std::runtime_error("Hyprland did not remove output '" + outputName
                             + "' within 3 seconds");
}

}  // namespace od
