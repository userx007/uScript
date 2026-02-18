#ifndef USHELL_CORE_TERMINAL_LINUX_H
#define USHELL_CORE_TERMINAL_LINUX_H

#include <ushell_core_printout.h>

#define _POSIX_C_SOURCE 200809L
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <termios.h>

class TerminalRAII {
private:
    static struct termios original_config; // Defined inside the class
    bool initialized;

    class ErrorLogger {
    public:
        static const char* getErrorMessage() {
            return strerror(errno);
        }
    };

public:
    TerminalRAII() : initialized(false) {
        if (!isatty(STDIN_FILENO)) {
            uSHELL_PRINTF("Not a valid terminal.\n");
            return;
        }

        struct termios config;
        if (tcgetattr(STDIN_FILENO, &original_config) == 0 &&
            tcgetattr(STDIN_FILENO, &config) == 0) {

            config.c_lflag &= ~(ICANON | ECHO);
            config.c_cc[VMIN] = 1;
            config.c_cc[VTIME] = 0;

            if (tcsetattr(STDIN_FILENO, TCSANOW, &config) == -1) {
                uSHELL_PRINTF("Failed to configure terminal: %s\n", ErrorLogger::getErrorMessage());
                return;
            }

            atexit([]() { restoreTerminal(); }); // Ensure cleanup on exit
            initialized = true;
        }

        setvbuf(stdin, nullptr, _IONBF, 0);
        //clear();
    }

    ~TerminalRAII() {
        if (initialized) {
            restoreTerminal();
        }
    }

private:
    static void restoreTerminal() {
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_config) == -1) {
            uSHELL_PRINTF("Failed to restore terminal settings: %s\n", strerror(errno));
        }
    }

public:
    void clear() {
        uSHELL_PRINTF("\033[H\033[J");
        fflush(stdout);
    }
};

// Define 'original_config' inside the header to prevent linker errors
struct termios TerminalRAII::original_config;

#endif // USHELL_CORE_TERMINAL_LINUX_H
