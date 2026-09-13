#ifndef USHELL_CORE_TERMINAL_H
#define USHELL_CORE_TERMINAL_H

#ifdef _WIN32
    #include "ushell_core_terminal_windows.h"
#else
    #include "ushell_core_terminal_linux.h"
#endif

#endif // USHELL_CORE_TERMINAL_H
