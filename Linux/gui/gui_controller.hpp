#pragma once

#include "frame_provider.hpp"

#include <QObject>
#include <QSettings>
#include <QThread>
#include <QVariantMap>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QTimer;

namespace od::gui {

class SessionWorker;

class GuiController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged)
    Q_PROPERTY(bool quitting READ quitting NOTIFY quittingChanged)
    Q_PROPERTY(QVariantMap savedSettings READ savedSettings CONSTANT)
    Q_PROPERTY(QString currentFrame READ currentFrame NOTIFY frameReady)
    Q_PROPERTY(int streamWidth READ streamWidth NOTIFY streamSizeChanged)
    Q_PROPERTY(int streamHeight READ streamHeight NOTIFY streamSizeChanged)

public:
    explicit GuiController(QObject* parent = nullptr);
    ~GuiController() override;

    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] QString detail() const { return detail_; }
    [[nodiscard]] bool connected() const { return connected_; }
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] bool trayAvailable() const { return trayAvailable_; }
    [[nodiscard]] bool quitting() const { return quitting_; }
    [[nodiscard]] QVariantMap savedSettings() const;
    [[nodiscard]] int streamWidth() const { return streamWidth_; }
    [[nodiscard]] int streamHeight() const { return streamHeight_; }
    [[nodiscard]] QString currentFrame() const { return currentFrame_; }
    /// The image provider QML uses to fetch the latest frame.
    [[nodiscard]] FrameProvider* frameProvider() { return &frameProvider_; }

    Q_INVOKABLE void connectDevice(const QVariantMap& values);
    Q_INVOKABLE void connectLast();
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE void quit();
    /// Update the advertised panel size in receiver mode (window resize).
    Q_INVOKABLE void setPanel(int width, int height, double scale);
    /// Send a touch event back to the sender (receiver mode).
    Q_INVOKABLE void sendTouch(const QString& phase, double x, double y);
    /// Send a scroll event back to the sender (receiver mode).
    Q_INVOKABLE void sendScroll(double dx, double dy);

signals:
    void stateChanged();
    void trayAvailableChanged();
    void quittingChanged();
    void showWindowRequested();
    /// Emitted with a decoded frame for the QML video surface (receiver mode).
    void frameReady();
    /// Emitted when the decoded stream's actual dimensions change.
    void streamSizeChanged();

private slots:
    void applyWorkerState(const QString& status, const QString& detail,
                          bool connected, bool busy);
    void updateTrayAvailability();

private:
    void createTray();
    void setLocalError(const QString& message);

    QSettings settings_;
    QVariantMap pendingSettings_;
    QThread workerThread_;
    SessionWorker* worker_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* trayMenu_ = nullptr;
    QAction* connectAction_ = nullptr;
    QAction* disconnectAction_ = nullptr;
    QTimer* trayPoll_ = nullptr;
    QString status_ = QStringLiteral("Disconnected");
    QString detail_ = QStringLiteral("Ready to connect.");
    QString currentFrame_;
    FrameProvider frameProvider_;
    int streamWidth_ = 0;
    int streamHeight_ = 0;
    bool connected_ = false;
    bool busy_ = false;
    bool trayAvailable_ = false;
    bool quitting_ = false;
};

}  // namespace od::gui
