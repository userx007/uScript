#ifndef TERMINAL_RAII_HPP
#define TERMINAL_RAII_HPP

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

class TerminalRAII
{
    public:

        TerminalRAII()
        {
            #if defined(_WIN32) || defined(_WIN64)
            hStdin = GetStdHandle(STD_INPUT_HANDLE);
            GetConsoleMode(hStdin, &originalMode);
            DWORD newMode = originalMode;
            newMode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
            SetConsoleMode(hStdin, newMode);
            #else
            tcgetattr(STDIN_FILENO, &originalTerm);
            termios rawTerm = originalTerm;
            rawTerm.c_lflag &= ~(ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &rawTerm);
            #endif
        }

        ~TerminalRAII()
        {
            #if defined(_WIN32) || defined(_WIN64)
            SetConsoleMode(hStdin, originalMode);
            #else
            tcsetattr(STDIN_FILENO, TCSANOW, &originalTerm);
            #endif
        }

        // Optional: Instant character read without waiting for Enter
        int readChar() const
        {
            #if defined(_WIN32) || defined(_WIN64)
            return _getch();
            #else
            return getchar();
            #endif
        }

    private:

        #if defined(_WIN32) || defined(_WIN64)
        HANDLE hStdin;
        DWORD originalMode;
        #else
        struct termios originalTerm;
        #endif
};

#endif // TERMINAL_RAII_HPP
