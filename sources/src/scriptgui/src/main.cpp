#include <QApplication>
#include <QSettings>
#include "MainWindow.hpp"
#include "AppStyle.hpp"

int main(int argc, char *argv[])
{
    // ── High-DPI support (Qt 6 has this on by default, but be explicit) ───
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);

    // ── Application identity (used by QSettings) ──────────────────────────
    app.setApplicationName("ScriptFrontend");
    app.setOrganizationName("ScriptTools");
    app.setApplicationVersion("1.0");

    // ── Global stylesheet ─────────────────────────────────────────────────
    app.setStyleSheet(appStyleSheet());

    // ── Main window ───────────────────────────────────────────────────────
    MainWindow w;
    w.show();

    return app.exec();
}
