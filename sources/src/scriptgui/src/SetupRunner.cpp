#include "SetupRunner.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QIODevice>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  Static helpers
// ─────────────────────────────────────────────────────────────────────────────
QString SetupRunner::setupFileName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("setup.bat");
#else
    return QStringLiteral("setup.sh");
#endif
}

bool SetupRunner::isSetupFileName(const QString &filePath)
{
    const QString n = QFileInfo(filePath).fileName();
    return n.compare(QLatin1String("setup.sh"), Qt::CaseInsensitive) == 0 ||
           n.compare(QLatin1String("setup.bat"), Qt::CaseInsensitive) == 0;
}

QString SetupRunner::findSetupScript(const QString &anyFileInFolder)
{
    if (anyFileInFolder.isEmpty()) {
        return {};
    }
    const QFileInfo candidate(QFileInfo(anyFileInFolder).absoluteDir().filePath(setupFileName()));
    return candidate.isFile() ? candidate.absoluteFilePath() : QString();
}

bool SetupRunner::needsElevation(const QString &scriptPath)
{
    QFile f(scriptPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QString text = QString::fromUtf8(f.read(1 << 20)); // setup scripts are small; cap at 1 MiB

#ifdef Q_OS_WIN
    static const QRegularExpression adminCmd(
        QStringLiteral(R"rx((?:^|[\s&|(@])(?:net\s+(?:start|stop|user|localgroup|accounts|share)|)rx"
                       R"rx(sc(?:\.exe)?\s+(?:create|config|start|stop|delete)|netsh|schtasks|bcdedit|dism|)rx"
                       R"rx(sfc|takeown|icacls|diskpart|pnputil|wmic|msiexec|choco|runas|)rx"
                       R"rx(reg(?:\.exe)?\s+(?:add|delete|import)\s+"?hk(?:lm|ey_local_machine))\b)rx"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("::")) ||
            line.compare(QLatin1String("rem"), Qt::CaseInsensitive) == 0 ||
            line.startsWith(QLatin1String("rem "), Qt::CaseInsensitive) ||
            line.startsWith(QLatin1String("rem\t"), Qt::CaseInsensitive)) {
            continue;
        }
        if (adminCmd.match(line).hasMatch()) {
            return true;
        }
    }
    return false;
#else
    // `sudo` used as a command: at line start or after ; & | ( ` { $(
    static const QRegularExpression sudoCmd(QStringLiteral(R"rx((?:^|[\s;&|(`{]|\$\()sudo(?:\s|$))rx"));
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (sudoCmd.match(line).hasMatch()) {
            return true;
        }
    }
    return false;
#endif
}

bool SetupRunner::isElevatedNow()
{
#ifdef Q_OS_WIN
    // `fltmc` succeeds only in an elevated process.
    QProcess p;
    p.start(QStringLiteral("fltmc"), {});
    if (!p.waitForFinished(5000)) {
        p.kill();
        return false;
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
#else
    if (::geteuid() == 0) {
        return true; // root: sudo (if used at all) never asks
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("sudo"), {QStringLiteral("-n"), QStringLiteral("true")});
    if (!p.waitForStarted(3000)) {
        return true; // no sudo installed: a password prompt would be pointless
    }
    if (!p.waitForFinished(5000)) {
        p.kill();
        return false;
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0; // passwordless / cached
#endif
}

bool SetupRunner::verifyPassword(const QString &password, QString *pError)
{
#ifdef Q_OS_WIN
    Q_UNUSED(password)
    Q_UNUSED(pError)
    return true; // credentials are only checked when the script is launched
#else
    QProcess p;
    p.setChildProcessModifier([] { ::setsid(); }); // no controlling tty → sudo can't prompt there
    p.start(QStringLiteral("sudo"),
            {QStringLiteral("-S"), QStringLiteral("-k"), QStringLiteral("-v"), QStringLiteral("-p"), QString()});
    if (!p.waitForStarted(3000)) {
        if (pError) {
            *pError = QStringLiteral("Could not start sudo.");
        }
        return false;
    }
    p.write(password.toUtf8() + '\n');
    p.closeWriteChannel();
    if (!p.waitForFinished(15000)) {
        p.kill();
        if (pError) {
            *pError = QStringLiteral("sudo did not answer in time.");
        }
        return false;
    }
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
        return true;
    }
    if (pError) {
        const QStringList lines = QString::fromLocal8Bit(p.readAllStandardError()).trimmed().split(QLatin1Char('\n'));
        QString msg             = lines.isEmpty() ? QString() : lines.last().trimmed();
        // sudo says "Sorry, try again." / "1 incorrect password attempt" — make it user-friendly.
        if (msg.isEmpty() || msg.contains(QLatin1String("try again"), Qt::CaseInsensitive) ||
            msg.contains(QLatin1String("incorrect password"), Qt::CaseInsensitive)) {
            msg = QStringLiteral("Incorrect password.");
        }
        *pError = msg;
    }
    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lifetime
// ─────────────────────────────────────────────────────────────────────────────
SetupRunner::SetupRunner(QObject *pParent)
    : QObject(pParent)
    , m_proc(new QProcess(this))
    , m_killTimer(new QTimer(this))
    , m_tailTimer(new QTimer(this))
{
    m_killTimer->setSingleShot(true);
    m_tailTimer->setInterval(400);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, &SetupRunner::onReadyOut);
    connect(m_proc, &QProcess::readyReadStandardError, this, &SetupRunner::onReadyErr);
    connect(m_proc, &QProcess::started, this, &SetupRunner::started);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        onProcessFinished(code, static_cast<int>(st));
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            emit outputLine(QStringLiteral("Could not start the setup script: %1").arg(m_proc->errorString()), true);
            cleanup();
            emit finished(-1, true);
        }
    });
    connect(m_tailTimer, &QTimer::timeout, this, &SetupRunner::pollTailFiles);
    connect(m_killTimer, &QTimer::timeout, this, [this] {
#ifdef Q_OS_UNIX
        if (isRunning()) {
            ::kill(-static_cast<pid_t>(m_proc->processId()), SIGKILL);
        }
#else
        m_proc->kill();
#endif
    });
}

SetupRunner::~SetupRunner()
{
    if (isRunning()) {
        disconnect(m_proc, nullptr, this, nullptr);
#ifdef Q_OS_UNIX
        ::kill(-static_cast<pid_t>(m_proc->processId()), SIGKILL);
#else
        m_proc->kill();
#endif
        m_proc->waitForFinished(2000);
    }
    cleanup();
}

bool SetupRunner::isRunning() const
{
    return m_proc->state() != QProcess::NotRunning;
}

void SetupRunner::stop()
{
    if (!isRunning()) {
        return;
    }
#ifdef Q_OS_UNIX
    ::kill(-static_cast<pid_t>(m_proc->processId()), SIGTERM);
    m_killTimer->start(3000);
#else
    m_proc->kill();
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Start
// ─────────────────────────────────────────────────────────────────────────────
void SetupRunner::start(const QString &scriptPath, const QString &user, const QString &password)
{
    if (isRunning()) {
        return;
    }
    m_outBuf.clear();
    m_errBuf.clear();
#ifdef Q_OS_WIN
    startWindows(scriptPath, user, password);
#else
    Q_UNUSED(user)
    startPosix(scriptPath, password);
#endif
}

#ifndef Q_OS_WIN
void SetupRunner::startPosix(const QString &scriptPath, const QString &password)
{
    const QFileInfo fi(scriptPath);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TERM"), QStringLiteral("dumb")); // no colour / cursor tricks from tput etc.

    QStringList args;
    if (password.isEmpty()) {
        args << fi.absoluteFilePath();
    } else {
        // Private helper that prints the password; sudo calls it instead of
        // reading a terminal.  Prefer $XDG_RUNTIME_DIR (tmpfs, per-user, exec
        // allowed) over /tmp, which is sometimes mounted noexec.
        const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        m_askpassDir          = runtime.isEmpty() ? std::make_unique<QTemporaryDir>()
                                                  : std::make_unique<QTemporaryDir>(runtime + QStringLiteral("/scriptgui-XXXXXX"));
        if (!m_askpassDir->isValid()) {
            m_askpassDir.reset();
            emit outputLine(QStringLiteral("Could not create a temporary folder for the sudo helper."), true);
            emit finished(-1, true);
            return;
        }
        QFile helper(m_askpassDir->filePath(QStringLiteral("askpass.sh")));
        if (!helper.open(QIODevice::WriteOnly)) {
            m_askpassDir.reset();
            emit outputLine(QStringLiteral("Could not create the sudo helper script."), true);
            emit finished(-1, true);
            return;
        }
        helper.write("#!/bin/sh\nprintf '%s\\n' \"$SCRIPTGUI_SUDO_PW\"\n");
        helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        helper.close();

        env.insert(QStringLiteral("SUDO_ASKPASS"), helper.fileName());
        env.insert(QStringLiteral("SCRIPTGUI_SUDO_PW"), password);

        // `sudo` becomes an exported function → always -A (askpass), even in
        // sub-shells, pipelines and when started from a terminal.
        args << QStringLiteral("-c")
             << QStringLiteral("sudo() { command sudo -A \"$@\"; }; export -f sudo; exec bash \"$0\"")
             << fi.absoluteFilePath();
    }

    m_proc->setProcessEnvironment(env);
    m_proc->setWorkingDirectory(fi.absolutePath());
    m_proc->setChildProcessModifier([] { ::setsid(); }); // own session: no tty, killable as a group
    m_proc->setProgram(QStringLiteral("bash"));
    m_proc->setArguments(args);
    m_proc->start(QIODevice::ReadOnly);
    m_proc->closeWriteChannel(); // scripts see EOF on stdin instead of hanging on a prompt
}
#else
void SetupRunner::startPosix(const QString &, const QString &) {}
#endif

#ifdef Q_OS_WIN
void SetupRunner::startWindows(const QString &scriptPath, const QString &user, const QString &password)
{
    const QFileInfo fi(scriptPath);
    const QString native = QDir::toNativeSeparators(fi.absoluteFilePath());
    m_proc->setWorkingDirectory(fi.absolutePath());

    if (user.isEmpty() || password.isEmpty()) {
        m_proc->setProgram(QStringLiteral("cmd.exe"));
        m_proc->setNativeArguments(QStringLiteral("/d /c \"\"%1\"\"").arg(native));
        m_proc->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        m_proc->start(QIODevice::ReadOnly);
        m_proc->closeWriteChannel();
        return;
    }

    // Credentialed run.  Output goes to files in %PUBLIC% (writable by every
    // account, unlike the caller's %TEMP%) which are tailed while the script runs.
    QString base = qEnvironmentVariable("PUBLIC");
    if (base.isEmpty()) {
        base = QDir::tempPath();
    }
    m_tailDir = base + QStringLiteral("/scriptgui_%1_%2")
                           .arg(QCoreApplication::applicationPid())
                           .arg(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(m_tailDir);
    m_tailOutPath = m_tailDir + QStringLiteral("/out.txt");
    m_tailErrPath = m_tailDir + QStringLiteral("/err.txt");
    m_tailOutPos = m_tailErrPos = 0;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SG_USER"), user);
    env.insert(QStringLiteral("SG_PW"), password);
    env.insert(QStringLiteral("SG_SCRIPT"), native);
    env.insert(QStringLiteral("SG_DIR"), QDir::toNativeSeparators(fi.absolutePath()));
    env.insert(QStringLiteral("SG_OUT"), QDir::toNativeSeparators(m_tailOutPath));
    env.insert(QStringLiteral("SG_ERR"), QDir::toNativeSeparators(m_tailErrPath));

    // No double quotes in the command text → no quoting pitfalls on the command line.
    const QString ps = QStringLiteral(
        "$ErrorActionPreference='Stop';"
        "$s=ConvertTo-SecureString $env:SG_PW -AsPlainText -Force;"
        "$c=New-Object System.Management.Automation.PSCredential($env:SG_USER,$s);"
        "$q=[char]34;"
        "$p=Start-Process -FilePath 'cmd.exe' -ArgumentList ('/d /c '+$q+$q+$env:SG_SCRIPT+$q+$q) "
        "-WorkingDirectory $env:SG_DIR -Credential $c -Wait -PassThru -WindowStyle Hidden "
        "-RedirectStandardOutput $env:SG_OUT -RedirectStandardError $env:SG_ERR;"
        "exit $p.ExitCode");

    m_proc->setProcessEnvironment(env);
    m_proc->setProgram(QStringLiteral("powershell.exe"));
    m_proc->setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                          QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                          QStringLiteral("-Command"), ps});
    m_tailTimer->start();
    m_proc->start(QIODevice::ReadOnly);
    m_proc->closeWriteChannel();
}
#else
void SetupRunner::startWindows(const QString &, const QString &, const QString &) {}
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  Output
// ─────────────────────────────────────────────────────────────────────────────
void SetupRunner::onReadyOut()
{
    feed(m_proc->readAllStandardOutput(), false);
}

void SetupRunner::onReadyErr()
{
    feed(m_proc->readAllStandardError(), true);
}

void SetupRunner::feed(const QByteArray &chunk, bool isError)
{
    QByteArray &buf = isError ? m_errBuf : m_outBuf;
    buf += chunk;
    int idx;
    while ((idx = buf.indexOf('\n')) >= 0) {
        emitLine(buf.left(idx), isError);
        buf.remove(0, idx + 1);
    }
}

void SetupRunner::flushPartial(bool isError)
{
    QByteArray &buf = isError ? m_errBuf : m_outBuf;
    if (!buf.isEmpty()) {
        emitLine(buf, isError);
        buf.clear();
    }
}

void SetupRunner::emitLine(QByteArray raw, bool isError)
{
    while (raw.endsWith('\r')) { // CRLF
        raw.chop(1);
    }
    // Progress bars redraw with a bare '\r': keep only the final state.
    const int cr = raw.lastIndexOf('\r');
    if (cr >= 0) {
        raw.remove(0, cr + 1);
    }
#ifdef Q_OS_WIN
    QString text = QString::fromLocal8Bit(raw);
#else
    QString text = QString::fromUtf8(raw);
#endif
    // Keep colour codes (the log viewer renders them), drop cursor/erase codes.
    static const QRegularExpression nonSgr(QStringLiteral("\x1b\\[[0-9;?]*[A-LN-Za-ln-z]"));
    text.remove(nonSgr);
    emit outputLine(text, isError);
}

void SetupRunner::pollTailFiles()
{
    auto readNew = [this](const QString &path, qint64 &pos, bool isError) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return; // not created yet / still locked: try again next tick
        }
        if (f.seek(pos)) {
            const QByteArray data = f.readAll();
            pos += data.size();
            feed(data, isError);
        }
    };
    if (!m_tailOutPath.isEmpty()) {
        readNew(m_tailOutPath, m_tailOutPos, false);
        readNew(m_tailErrPath, m_tailErrPos, true);
    }
}

void SetupRunner::onProcessFinished(int exitCode, int exitStatus)
{
    m_killTimer->stop();
    m_tailTimer->stop();
    feed(m_proc->readAllStandardOutput(), false);
    feed(m_proc->readAllStandardError(), true);
    pollTailFiles(); // last bytes of the redirect files (Windows credentialed run)
    flushPartial(false);
    flushPartial(true);
    cleanup();
    emit finished(exitCode, exitStatus == static_cast<int>(QProcess::CrashExit));
}

void SetupRunner::cleanup()
{
    m_tailTimer->stop();
    m_askpassDir.reset(); // removes the askpass helper
    if (!m_tailDir.isEmpty()) {
        QDir(m_tailDir).removeRecursively();
        m_tailDir.clear();
        m_tailOutPath.clear();
        m_tailErrPath.clear();
    }
}
