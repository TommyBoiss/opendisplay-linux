#pragma once

#include "opendisplay/desktop_backend.hpp"
#include "opendisplay/kde_output_controller.hpp"

#include <QDBusConnection>
#include <QVariantMap>
#include <QObject>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace od {

/// KDE implementation of the platform capture/output/input boundary. It uses
/// the public RemoteDesktop + ScreenCast portals, so future compositor
/// backends can implement the same surface without touching Session.
class KdePortal final : public QObject, public DesktopBackend {
    Q_OBJECT

public:
    explicit KdePortal(QObject* parent = nullptr);
    ~KdePortal() override;

    PortalCapture start(const DesktopRequest& request) override;
    void stop() override;
    void pointer(std::string_view phase, double normalizedX, double normalizedY) override;
    void scroll(double dx, double dy) override;

private slots:
    void requestResponse(uint response, const QVariantMap& results);

private:
    QVariantMap request(const QString& interface, const QString& method,
                        const QVariantList& arguments, QVariantMap options);
    static QVariant unwrap(QVariant value);
    static std::optional<PortalStream> firstStream(const QVariant& value,
                                                   int fallbackWidth,
                                                   int fallbackHeight);
    void notify(const QString& method, const QVariantList& arguments);

    QDBusConnection bus_;
    QString sessionPath_;
    PortalStream stream_;
    bool inputEnabled_ = false;
    bool pointerDown_ = false;
    bool waiting_ = false;
    uint responseCode_ = 2;
    QVariantMap responseResults_;
    KdeOutputController outputs_;
    std::optional<DisplayOutput> virtualOutput_;
    std::vector<DisplayOutput> outputsBefore_;
    OutputTranslation outputTranslation_;
};

}  // namespace od
