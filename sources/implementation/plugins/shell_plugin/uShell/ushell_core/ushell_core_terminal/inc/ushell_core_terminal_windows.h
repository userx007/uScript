#ifndef USHELL_CORE_TERMINAL_WINDOWS_H
#define USHELL_CORE_TERMINAL_WINDOWS_H


#include <ushell_core_printout.h>
#include <windows.h>

#ifdef _MSC_VER
#pragma warning(disable : 4710)
#endif

class TerminalRAII {
private:
    HANDLE hConsole;

    // Private class for error handling, only accessible inside TerminalRAII
    class WindowsError {
    public:
        static const char *getErrorMessage() {
            static char errorMsg[256]; // Persistent buffer
            DWORD errorCode = GetLastError();
            if (errorCode == 0) {
                return "No error.";
            }

            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, errorCode, 0, errorMsg, sizeof(errorMsg), nullptr);

            return errorMsg;
        }
    };

public:
    // Acquire the console handle and enable VT mode
    TerminalRAII() {
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hConsole == INVALID_HANDLE_VALUE) {
            uSHELL_PRINTF("Invalid handle for console output\n");
            hConsole = nullptr;
            return;
        }

        DWORD dwMode = 0;
        if (!GetConsoleMode(hConsole, &dwMode)) {
            uSHELL_PRINTF("Failed to get console mode. Error: %s\n", WindowsError::getErrorMessage());
            hConsole = nullptr;
            return;
        }

        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(hConsole, dwMode)) {
            uSHELL_PRINTF("Unable to enter VT processing mode. Error: %s\n", WindowsError::getErrorMessage());
            hConsole = nullptr;
        }
    }

    // Automatically clear the terminal on object destruction
    ~TerminalRAII() {
        if (!hConsole) return;

        COORD coordScreen = {0, 0};
        DWORD cCharsWritten;
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        DWORD dwConSize;

        if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
            uSHELL_PRINTF("Failed to retrieve console buffer info. Error: %s\n", WindowsError::getErrorMessage());
            return;
        }

        dwConSize = (DWORD)(csbi.dwSize.X * csbi.dwSize.Y);
        FillConsoleOutputCharacter(hConsole, ' ', dwConSize, coordScreen, &cCharsWritten);
        FillConsoleOutputAttribute(hConsole, csbi.wAttributes, dwConSize, coordScreen, &cCharsWritten);
        SetConsoleCursorPosition(hConsole, coordScreen);

        uSHELL_PRINTF("Terminal restored ok\n");
    }
};

#endif // USHELL_CORE_TERMINAL_WINDOWS_H