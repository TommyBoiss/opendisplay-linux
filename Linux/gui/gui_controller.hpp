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
    Q_PROPERTY(bool cursorVisible READ cursorVisible NOTIFY cursorChanged)
    Q_PROPERTY(double cursorX READ cursorX NOTIFY cursorChanged)
    Q_PROPERTY(double cursorY READ cursorY NOTIFY cursorChanged)
    Q_PROPERTY(double cursorWidth READ cursorWidth NOTIFY cursorChanged)
    Q_PROPERTY(double cursorHeight READ cursorHeight NOTIFY cursorChanged)
    Q_PROPERTY(double cursorAnchorX READ cursorAnchorX NOTIFY cursorChanged)
    Q_PROPERTY(double cursorAnchorY READ cursorAnchorY NOTIFY cursorChanged)
    Q_PROPERTY(QString cursorImage READ cursorImage NOTIFY cursorChanged)

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
    [[nodiscard]] bool cursorVisible() const { return cursorVisible_; }
    [[nodiscard]] double cursorX() const { return cursorX_; }
    [[nodiscard]] double cursorY() const { return cursorY_; }
    [[nodiscard]] double cursorWidth() const { return cursorWidth_; }
    [[nodiscard]] double cursorHeight() const { return cursorHeight_; }
    [[nodiscard]] double cursorAnchorX() const { return cursorAnchorX_; }
    [[nodiscard]] double cursorAnchorY() const { return cursorAnchorY_; }
    [[nodiscard]] QString cursorImage() const { return cursorImage_; }
    /// The image provider QML uses to fetch the latest frame.
    [[nodiscard]] FrameProvider* frameProvider() { return &frameProvider_; }
    /// The image provider QML uses to fetch the latest cursor sprite.
    [[nodiscard]] FrameProvider* cursorProvider() { return &cursorProvider_; }

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
    /// Emitted when the sender updates the cursor sprite or position.
    void cursorChanged();

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
    FrameProvider cursorProvider_;
    int streamWidth_ = 0;
    int streamHeight_ = 0;
    bool cursorVisible_ = false;
    double cursorX_ = 0.0;
    double cursorY_ = 0.0;
    double cursorWidth_ = 0.0;
    double cursorHeight_ = 0.0;
    double cursorAnchorX_ = 0.0;
    double cursorAnchorY_ = 0.0;
    QString cursorImage_;
    bool connected_ = false;
    bool busy_ = false;
    bool trayAvailable_ = false;
    bool quitting_ = false;
};

}  // namespace od::gui
