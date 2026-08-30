#include "frame_provider.hpp"

namespace od::gui {

FrameProvider::FrameProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage FrameProvider::requestImage(const QString& id, QSize* size,
                                    const QSize& requestedSize) {
    std::lock_guard lock(mutex_);
    if (size) {
        *size = frame_.size();
    }
    // Return the frame as-is; QML's Image will scale it if requestedSize is set.
    return frame_;
}

void FrameProvider::setFrame(const QImage& image) {
    std::lock_guard lock(mutex_);
    frame_ = image.copy();
}

}  // namespace od::gui
