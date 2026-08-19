#ifndef UCHECKCONTINUE_HPP
#define UCHECKCONTINUE_HPP

#include "uTerminal.hpp"
#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "BREAKPOINT  |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                     CLASS IMPLEMENTATION                      //
///////////////////////////////////////////////////////////////////

// CheckContinue — interactive breakpoint gate.
//
// Suspends execution and waits for a single keypress:
//   a / A        → ask for abort confirmation (y/Y = abort, n/N = continue)
//   Space        → skip (only when pbSkip != nullptr)
//   any other    → continue normally
//
// Returns true  → caller should continue execution.
// Returns false → caller should abort (propagated as command failure).
//
// All output goes through LOG_PRINT(LOG_EMPTY) so it appears in the
// standard log stream rather than bypassing it via std::cout.

class CheckContinue
{
    public:

        // label    — optional context string printed in the prompt (e.g. script label)
        // pbSkip   — when non-null, Space sets *pbSkip = true and continues normally
        bool operator()(const std::string& label = "", bool* pbSkip = nullptr) const
        {
            bool bContinue = true;
            const bool bSkipable = (pbSkip != nullptr);

            // Print the prompt
            if (!label.empty()) {
                LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("--- BREAKPOINT"); LOG_STRING(label); LOG_STRING("---"));
            } else {
                LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("--- BREAKPOINT ---"));
            }

            if (bSkipable) {
                LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("Press a/A to abort, Space to skip, any other key to continue..."));
            } else {
                LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("Press a/A to abort, any other key to continue..."));
            }

            TerminalRAII terminal;
            const char key = terminal.readChar();

            if (key == 'A' || key == 'a') {
                LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("Aborting, are you sure? (y/n)"));
                while (true) {
                    const char confirm = terminal.readChar();
                    if (confirm == 'y' || confirm == 'Y') {
                        LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("Aborted by user."));
                        bContinue = false;
                        break;
                    } else if (confirm == 'n' || confirm == 'N') {
                        LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("Continuing..."));
                        break;
                    }
                    // Any other key: re-prompt silently — the loop keeps waiting
                }
            } else if (key == ' ' && bSkipable) {
                LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("Skipped by user."));
                *pbSkip = true;
            } else {
                LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("Continuing..."));
            }

            return bContinue;
        }
};

#endif // UCHECKCONTINUE_HPP

