#include "AppStyle.hpp"
#include "MainWindow.hpp"

#include <QApplication>
#include <QSettings>
#include <Qt>

int main(int iArgc, char *argv[])
{
    // ── High-DPI support (Qt 6 has this on by default, but be explicit) ───
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(iArgc, argv);

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
