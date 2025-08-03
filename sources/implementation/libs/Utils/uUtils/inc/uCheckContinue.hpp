#ifndef UCHECKCONTINUE_HPP
#define UCHECKCONTINUE_HPP

#include <iostream>
#include <string>

#if defined(_WIN32)
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

class CheckContinue
{

    public:

        explicit CheckContinue(const std::string& header = "")
            : m_header(header)
        {}

        bool operator()(bool* pbSkip = nullptr) const
        {
            bool bContinue = true;
            bool bSkipable = (pbSkip != nullptr);


            if (!m_header.empty())
                std::cout << m_header << "\n";
            std::cout << "Press Esc to abort, ";
            if (bSkipable) {
                std::cout << "Space to skip, ";
            }
            std::cout << "any other key to continue ..." << std::endl;

            char key = getChar();
            std::cout << "key: " << key << std::endl;

            if (key == 9) { // Tab key
                std::cout << "\nAborting, are you sure? (y/n): ";
                while (true) {
                    char confirm = getChar();
                    if (confirm == 'y' || confirm == 'Y') {
                        std::cout << "\nAborted by user!" << std::endl;
                        bContinue = false;
                        break;
                    } else if (confirm == 'n' || confirm == 'N') {
                        std::cout << "\nContinuing..." << std::endl;
                        break;
                    }
                }
            } else if (key == ' ') {
                if (bSkipable) {
                    std::cout << "\nSkipped by user!" << std::endl;
                    *pbSkip = true;
                }
            }

            return bContinue;
        }

    private:

        std::string m_header;

        static char getChar()
        {
#if defined(_WIN32)
            return _getch();
#else
            char buf = 0;
            struct termios original_config;

            if (!isatty(STDIN_FILENO)) {
                std::cout << "Not a valid terminal.\n";
                return buf;
            }

            struct termios config;
            if (tcgetattr(STDIN_FILENO, &original_config) == 0 &&
                tcgetattr(STDIN_FILENO, &config) == 0) {

                config.c_lflag &= ~(ICANON | ECHO);
                config.c_cc[VMIN] = 1;
                config.c_cc[VTIME] = 0;

                if (tcsetattr(STDIN_FILENO, TCSANOW, &config) == -1) {
                    std::cout << "Failed to configure terminal\n";
                    return buf;
                }
            }
            read(STDIN_FILENO, &buf, 1); // Read one character

            if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_config) == -1) {
                std::cout << "Failed to restore terminal settings\n";
            }
            return buf;
#endif
        }
};

#endif // UCHECKCONTINUE_HPP

#if 0

#include "CheckContinue.hpp"

int main()
{
    bool skip = false;
    CheckContinue prompt("Demo Header");

    if (!prompt(&skip)) {
        std::cout << "Exiting based on user choice\n";
    } else if (skip) {
        std::cout << "Skipping next step\n";
    } else {
        std::cout << "Proceeding\n";
    }

    return 0;
}

#endif