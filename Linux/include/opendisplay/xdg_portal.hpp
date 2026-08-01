#pragma once

#include "opendisplay/desktop_backend.hpp"

#include <QDBusConnection>
#include <QObject>
#include <QVariantMap>

#include <optional>

namespace od {

/// Shared client-side mechanics for ScreenCast and RemoteDesktop portals.
/// Compositor backends remain responsible for choosing the portal workflow.
class XdgPortal final : public QObject {
    Q_OBJECT

public:
    explicit XdgPortal(QObject* parent = nullptr);

    QVariantMap request(const QString& interface, const QString& method,
                        const QVariantList& arguments, QVariantMap options);
    QString createSession(const QString& interface);
    int openPipeWireRemote(const QString& sessionPath);
    void closeSession(const QString& sessionPath);
    void callNoReply(const QString& interface, const QString& method,
                     const QVariantList& arguments);
    QVariant property(const QString& interface, const char* name) const;

    static QVariant unwrap(QVariant value);
    static std::optional<PortalStream> firstStream(const QVariant& value,
                                                   int fallbackWidth,
                                                   int fallbackHeight);

private slots:
    void requestResponse(uint response, const QVariantMap& results);

private:
    QDBusConnection bus_;
    bool waiting_ = false;
    uint responseCode_ = 2;
    QVariantMap responseResults_;
};

}  // namespace od
