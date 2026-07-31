#include "opendisplay/discovery.hpp"
#include "opendisplay/kde_portal.hpp"
#include "opendisplay/log.hpp"
#include "opendisplay/session.hpp"
#include "opendisplay/types.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

std::atomic_bool interrupted = false;

void handleSignal(int) { interrupted.store(true); }

od::TransportKind transport(const QString& value) {
    if (value == QStringLiteral("auto")) return od::TransportKind::Auto;
    if (value == QStringLiteral("wifi")) return od::TransportKind::Wifi;
    if (value == QStringLiteral("usb")) return od::TransportKind::Usb;
    throw std::runtime_error("--transport must be auto, wifi, or usb");
}

od::CaptureMode mode(const QString& value) {
    if (value == QStringLiteral("extend")) return od::CaptureMode::Extend;
    if (value == QStringLiteral("mirror")) return od::CaptureMode::Mirror;
    throw std::runtime_error("--mode must be extend or mirror");
}

od::EncoderKind encoder(const QString& value) {
    if (value == QStringLiteral("auto")) return od::EncoderKind::Auto;
    if (value == QStringLiteral("vaapi")) return od::EncoderKind::Vaapi;
    if (value == QStringLiteral("nvenc")) return od::EncoderKind::Nvenc;
    if (value == QStringLiteral("software")) return od::EncoderKind::Software;
    throw std::runtime_error("--encoder must be auto, vaapi, nvenc, or software");
}

int positiveInt(const QCommandLineParser& parser, const QCommandLineOption& option) {
    bool valid = false;
    const int result = parser.value(option).toInt(&valid);
    if (!valid || result <= 0) {
        throw std::runtime_error(("invalid --" + option.names().front()).toStdString());
    }
    return result;
}

void printEndpoint(const od::Endpoint& endpoint) {
    if (endpoint.kind == od::TransportKind::Usb) {
        std::cout << "usb   " << endpoint.udid << "  " << endpoint.name << '\n';
    } else {
        std::cout << "wifi  " << endpoint.host << ':' << endpoint.port << "  "
                  << endpoint.name << '\n';
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    std::signal(SIGPIPE, SIG_IGN);
    QCoreApplication::setApplicationName(QStringLiteral("opendisplay-linux"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Use an unchanged OpenDisplay iOS app as a KDE Wayland display."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption transportOption({"t", "transport"},
        "Connection method: auto, wifi, or usb.", "kind", "auto");
    const QCommandLineOption hostOption("host", "Connect directly to a Wi-Fi host.", "address");
    const QCommandLineOption portOption({"p", "port"}, "Receiver TCP port.", "port", "9000");
    const QCommandLineOption serviceOption("service", "Select a Bonjour service name.", "name");
    const QCommandLineOption udidOption("udid", "Select a USB device by UDID.", "udid");
    const QCommandLineOption modeOption({"m", "mode"}, "Display mode: extend or mirror.",
                                        "mode", "extend");
    const QCommandLineOption encoderOption({"e", "encoder"},
        "FFmpeg encoder: auto, vaapi, nvenc, or software.", "encoder", "auto");
    const QCommandLineOption vaapiOption("vaapi-device", "VA-API render node.", "path",
                                         "/dev/dri/renderD128");
    const QCommandLineOption fpsOption("fps", "Maximum frame rate.", "fps", "60");
    const QCommandLineOption bitrateOption("bitrate", "H.264 bitrate in bits/second.",
                                            "bps", "18000000");
    const QCommandLineOption scaleOption("scale", "Encoded resolution multiplier.", "factor",
                                          "1.0");
    const QCommandLineOption noInputOption("no-input", "Do not request pointer control.");
    const QCommandLineOption listOption({"l", "list"}, "List visible Wi-Fi and USB receivers.");
    const QCommandLineOption verboseOption("verbose", "Enable diagnostic logging.");
    parser.addOptions({transportOption, hostOption, portOption, serviceOption, udidOption,
                       modeOption, encoderOption, vaapiOption, fpsOption, bitrateOption,
                       scaleOption, noInputOption, listOption, verboseOption});
    parser.process(application);

    try {
        od::Options options;
        options.transport = transport(parser.value(transportOption));
        options.mode = mode(parser.value(modeOption));
        options.encoder = encoder(parser.value(encoderOption));
        options.host = parser.value(hostOption).toStdString();
        const int port = positiveInt(parser, portOption);
        if (port > 65'535) {
            throw std::runtime_error("--port must be between 1 and 65535");
        }
        options.port = static_cast<std::uint16_t>(port);
        options.serviceName = parser.value(serviceOption).toStdString();
        options.udid = parser.value(udidOption).toStdString();
        options.vaapiDevice = parser.value(vaapiOption).toStdString();
        options.fps = positiveInt(parser, fpsOption);
        options.bitrate = positiveInt(parser, bitrateOption);
        bool scaleValid = false;
        options.scale = parser.value(scaleOption).toDouble(&scaleValid);
        if (!scaleValid || options.scale <= 0 || options.scale > 1) {
            throw std::runtime_error("--scale must be greater than 0 and at most 1");
        }
        options.input = !parser.isSet(noInputOption);
        options.listDevices = parser.isSet(listOption);
        options.verbose = parser.isSet(verboseOption);
        od::verboseLogging = options.verbose;

        if (options.listDevices) {
            try {
                for (const auto& endpoint : od::discoverUsb()) printEndpoint(endpoint);
            } catch (const std::exception& error) {
                od::log(error.what());
            }
            try {
                for (const auto& endpoint : od::discoverWifi(std::chrono::seconds(3))) {
                    printEndpoint(endpoint);
                }
            } catch (const std::exception& error) {
                od::log(error.what());
            }
            return 0;
        }

        const auto endpoint = od::chooseEndpoint(options);
        od::Session session(options, std::make_unique<od::KdePortal>());
        session.start(endpoint);
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &application, [&] {
            try {
                if (interrupted.load() || !session.tick()) {
                    application.quit();
                }
            } catch (const std::exception& error) {
                od::log(std::string("Session error: ") + error.what());
                application.exit(1);
            }
        });
        timer.start(20);
        const int result = application.exec();
        session.stop();
        return result;
    } catch (const std::exception& error) {
        std::cerr << "opendisplay-linux: " << error.what() << '\n';
        return 1;
    }
}
