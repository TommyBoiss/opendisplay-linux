#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <mutex>

namespace od::gui {

/// Fast QML image provider: caches the latest decoded frame and serves it
/// directly to the scene graph without per-frame PNG encoding.
class FrameProvider final : public QQuickImageProvider {
public:
    explicit FrameProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    /// Update the cached frame (called from frameReady).
    void setFrame(const QImage& image);

private:
    mutable std::mutex mutex_;
    QImage frame_;
};

}  // namespace od::gui
