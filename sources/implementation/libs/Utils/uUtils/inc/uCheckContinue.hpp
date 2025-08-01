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

            if (key == 27) { // ESC key
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
            termios old = {};
            if (tcgetattr(STDIN_FILENO, &old) < 0)
                return buf;
            termios new_term = old;
            new_term.c_lflag &= ~(ICANON | ECHO);
            if (tcsetattr(STDIN_FILENO, TCSANOW, &new_term) < 0)
                return buf;
            ssize_t n = read(STDIN_FILENO, &buf, 1);
            if (n < 0) {
                std::cout << "read failed\n";
                return '\0';
            } else if (n == 0) {
                return '\0';
            }
            tcsetattr(STDIN_FILENO, TCSANOW, &old);
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