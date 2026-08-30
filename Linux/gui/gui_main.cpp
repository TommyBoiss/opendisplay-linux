#include "gui_controller.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QStyle>
#include <QVariant>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("OpenDisplay"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("OpenDisplay"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.opendisplay.desktop"));
    QApplication::setQuitOnLastWindowClosed(false);
    QIcon applicationIcon = QIcon::fromTheme(QStringLiteral("video-display"));
    if (applicationIcon.isNull()) {
        applicationIcon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    QApplication::setWindowIcon(applicationIcon);

    od::gui::GuiController controller;
    QQmlApplicationEngine engine;
    // Register the fast image provider so QML's Image can fetch decoded
    // frames directly from the scene graph (image://opendisplay/frame).
    engine.addImageProvider(QStringLiteral("opendisplay"), controller.frameProvider());
    engine.setInitialProperties({
        {QStringLiteral("controller"), QVariant::fromValue(&controller)},
    });
    engine.loadFromModule(QStringLiteral("org.opendisplay.desktop"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 1;
    return application.exec();
}
