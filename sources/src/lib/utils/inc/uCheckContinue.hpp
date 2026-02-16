#ifndef UCHECKCONTINUE_HPP
#define UCHECKCONTINUE_HPP

#include "uTerminal.hpp"

#include <iostream>

class CheckContinue
{
    public:

        bool operator()(bool* pbSkip = nullptr) const
        {
            bool bContinue = true;
            bool bSkipable = (pbSkip != nullptr);

            TerminalRAII terminal;

            std::cout << "Press a/A to abort, ";
            if (bSkipable) {
                std::cout << "Space to skip, ";
            }
            std::cout << "any other key to continue ..." << std::endl;

            char key = terminal.readChar();
            if ((key == 'A') || (key == 'a')) { // A/a key for abort
                std::cout << "Aborting, are you sure? (y/n): ";
                while (true) {
                    char confirm = terminal.readChar();
                    if (confirm == 'y' || confirm == 'Y') {
                        std::cout << "\nAborted by user!" << std::endl;
                        bContinue = false;
                        break;
                    } else if (confirm == 'n' || confirm == 'N') {
                        std::cout << "\nContinuing..." << std::endl;
                        break;
                    }
                }
            } else if (key == ' ') { // Space key for skip
                if (bSkipable) {
                    std::cout << "\nSkipped by user!" << std::endl;
                    *pbSkip = true;
                }
            }

            return bContinue;
        }
};

#endif // UCHECKCONTINUE_HPP

