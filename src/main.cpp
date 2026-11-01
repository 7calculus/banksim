// BankSim - multithreaded banking / ATM simulator (Qt 6 Widgets front-end).
#include <QApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QTimer>

#include "gui/mainwindow.h"
#include "gui/theme.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("BankSim");
    QApplication::setApplicationDisplayName("BankSim");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("BankSim");
    theme::apply(app);
    app.setWindowIcon(QIcon(":/banksim.png"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Multithreaded banking & ATM simulator");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption tourOpt("tour", "Developer aid: drive every screen and save screenshots into <dir>, then exit.", "dir");
    parser.addOption(tourOpt);
    parser.process(app);

    MainWindow window;
    if (parser.isSet(tourOpt)) {
        const bool ok = window.runScreenshotTour(parser.value(tourOpt));
        return ok ? 0 : 1;
    }
    window.show();
    return app.exec();
}
