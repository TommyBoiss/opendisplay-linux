#pragma once

#include "opendisplay/session.hpp"
#include "opendisplay/types.hpp"

#include <QObject>

#include <memory>

class QTimer;

namespace od::gui {

class SessionWorker final : public QObject {
    Q_OBJECT

public:
    explicit SessionWorker(QObject* parent = nullptr);

public slots:
    void start(od::Options options);
    void stop();

signals:
    void stateChanged(const QString& status, const QString& detail,
                      bool connected, bool busy);

private slots:
    void tick();

private:
    QTimer* timer_ = nullptr;
    std::unique_ptr<od::Session> session_;
};

}  // namespace od::gui
