#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <memory>

class QProcess;
class QTemporaryDir;
class QTimer;

/**
 * @brief Locates and runs the per-project "setup" script (setup.sh / setup.bat).
 *
 * The class has no GUI dependency: MainWindow owns the SETUP button and the
 * password dialog, SetupRunner only knows how to
 *   - find the setup script next to the opened script,
 *   - decide whether the script is likely to need elevated rights,
 *   - run it (optionally with a sudo / administrator password),
 *   - stream its output back line by line.
 *
 * ── Linux ───────────────────────────────────────────────────────────────────
 *   The script is run as `bash setup.sh` in its own folder and its own session
 *   (setsid), so sudo can never block on a terminal prompt.  When a password is
 *   supplied, `sudo` inside the script is transparently routed through
 *   SUDO_ASKPASS (a private 0700 helper that prints the password), so scripts
 *   that pipe into sudo or use it several times keep working.  Without a
 *   password the script is simply executed.
 *
 * ── Windows ─────────────────────────────────────────────────────────────────
 *   Without credentials: `cmd /d /c setup.bat`.
 *   With credentials: PowerShell Start-Process -Credential (secondary logon),
 *   output captured through redirect files that are tailed while it runs.
 */
class SetupRunner : public QObject
{
        Q_OBJECT
    public:
        explicit SetupRunner(QObject *pParent = nullptr);
        ~SetupRunner() override;

        // "setup.sh" on Linux/macOS, "setup.bat" on Windows.
        static QString setupFileName();

        // True for setup.sh / setup.bat (any platform).  Such files are shell
        // scripts, not µScripts: no µScript highlighting, never sent to RUN.
        static bool isSetupFileName(const QString &filePath);

        // Absolute path of the setup script located in the same folder as
        // `anyFileInFolder`, or an empty string when there is none.
        static QString findSetupScript(const QString &anyFileInFolder);

        // Heuristic: does the script look like it needs sudo / admin rights?
        // Linux: a `sudo` command outside comment lines.
        // Windows: well-known admin-only commands (net start, sc, netsh, ...).
        static bool needsElevation(const QString &scriptPath);

        // True when no password is required right now: already root / admin, or
        // (Linux) sudo works without a password or has cached credentials.
        static bool isElevatedNow();

        // Linux: checks the sudo password (`sudo -S -k -v`).  On failure *pError
        // receives sudo's message.  Windows: not verifiable up front, returns true.
        static bool verifyPassword(const QString &password, QString *pError = nullptr);

        /** Returns whether the setup process is running. */
        bool isRunning() const;

        // Start the script.  `user` is only used on Windows (empty = no
        // credentials).  An empty `password` means "run without elevation".
        void start(const QString &scriptPath, const QString &user, const QString &password);

        // Ask the running script to stop (SIGTERM to its process group, SIGKILL
        // after a short grace period / TerminateProcess on Windows).
        void stop();

    signals:
        /** Emitted for each complete line of output. */
        void outputLine(const QString &text, bool isError);
        /** Emitted when the setup process has started. */
        void started();
        /** Emitted when the process ends; @p crashed tells whether it crashed. */
        void finished(int exitCode, bool crashed);

    private:
        void onReadyOut();
        /** Reads pending stderr. */
        void onReadyErr();
        /** Drains remaining output, stops the timers and emits finished(). */
        void onProcessFinished(int exitCode, int exitStatus);
        /** Appends @p chunk to the stdout/stderr buffer and emits every complete line. */
        void feed(const QByteArray &chunk, bool isError);
        /** Emits any unterminated trailing line. */
        void flushPartial(bool isError);
        /** Strips the line ending and emits the line. */
        void emitLine(QByteArray raw, bool isError);
        /** Stops the timers and removes the temporary helper files. */
        void cleanup();
        /** Runs @p scriptPath via sudo, feeding @p password through an askpass helper. */
        void startPosix(const QString &scriptPath, const QString &password);
        /** Runs @p scriptPath as @p user with @p password. */
        void startWindows(const QString &scriptPath, const QString &user, const QString &password);
        /** Reads new output from the redirected stdout/stderr files. */
        void pollTailFiles();

        QProcess *m_proc = nullptr;
        QByteArray m_outBuf;
        QByteArray m_errBuf;
        std::unique_ptr<QTemporaryDir> m_askpassDir; // Linux: private askpass helper
        QTimer *m_killTimer = nullptr;               // SIGKILL fallback after stop()

        // Windows credentialed run: output redirect files, tailed by a timer
        QString m_tailDir;
        QString m_tailOutPath;
        QString m_tailErrPath;
        qint64 m_tailOutPos = 0;
        qint64 m_tailErrPos = 0;
        QTimer *m_tailTimer = nullptr;
};
