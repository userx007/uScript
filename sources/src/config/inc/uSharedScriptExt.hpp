#ifndef USHARED_SCRIPT_EXT_HPP
#define USHARED_SCRIPT_EXT_HPP

// Single source of truth for the file extensions that identify what a script file is.
// Header-only, standard C++ only (no Qt) so the interpreter and the Qt front-end share it.
//
//   .main  script run directly by the interpreter ("main"/core script); also the only
//          kind an INCLUDE directive may reference
//   .comm  communication script, run by a plugin (PLUGIN.SCRIPT <file>, PLUGIN.<CMD> script <file>)
//   .ini   interpreter configuration
//   shell scripts (platform specific, edited by the GUI but never executed as µScripts):
//     Linux/macOS : .sh
//     Windows     : .bat  .cmd  .ps1 (PowerShell)
//   A shell script of the *other* platform is not a valid file here (a .sh is not opened on
//   Windows, a .cmd/.bat/.ps1 is not opened on Linux).
//
// Comparison is case-insensitive (Windows/macOS file systems are), and a file needs a base
// name: a bare ".main" is not a script.

#include "uSharedConfig.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace scriptext
{
    inline bool hasExtension(std::string_view path, std::string_view ext)
    {
        const size_t slash = path.find_last_of("/\\");
        const std::string_view name = (slash == std::string_view::npos) ? path : path.substr(slash + 1);
        if (name.size() <= ext.size()) {
            return false;
        }
        const std::string_view tail = name.substr(name.size() - ext.size());
        return std::equal(tail.begin(), tail.end(), ext.begin(),
                          [](char a, char b) {
                              return std::tolower(static_cast<unsigned char>(a)) ==
                                     std::tolower(static_cast<unsigned char>(b));
                          });
    }

    inline bool isMainScript(std::string_view path) { return hasExtension(path, SCRIPT_EXT_MAIN); }
    inline bool isCommScript(std::string_view path) { return hasExtension(path, SCRIPT_EXT_COMM); }
    inline bool isIniFile(std::string_view path) { return hasExtension(path, SCRIPT_EXT_INI); }

#ifdef _WIN32
    inline constexpr bool kHostIsWindows = true;
#else
    inline constexpr bool kHostIsWindows = false;
#endif

    /** Shell script of the given platform (the explicit form exists so both rules can be tested anywhere). */
    inline bool isShellScriptFor(std::string_view path, bool bWindows)
    {
        return bWindows ? (hasExtension(path, SCRIPT_EXT_BAT) || hasExtension(path, SCRIPT_EXT_CMD) ||
                           hasExtension(path, SCRIPT_EXT_PS1))
                        : hasExtension(path, SCRIPT_EXT_SH);
    }
    /** Shell script of the platform this binary runs on. */
    inline bool isShellScript(std::string_view path) { return isShellScriptFor(path, kHostIsWindows); }

    /** Files the GUI's main editor may open: .main, .ini and this platform's shell scripts. */
    inline bool isMainEditorFileFor(std::string_view path, bool bWindows)
    {
        return isMainScript(path) || isIniFile(path) || isShellScriptFor(path, bWindows);
    }
    inline bool isMainEditorFile(std::string_view path) { return isMainEditorFileFor(path, kHostIsWindows); }
} // namespace scriptext

#endif // USHARED_SCRIPT_EXT_HPP
