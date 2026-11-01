#ifndef FILE_TYPES_HPP
#define FILE_TYPES_HPP

// Qt-side view of the file-extension rules (the rules themselves live in
// uSharedScriptExt.hpp, shared with the interpreter):
//
//   main editor / tabs : .main  .ini  + shell scripts of THIS platform (only .main can be RUN)
//                          Linux/macOS: .sh      Windows: .bat .cmd .ps1
//   comm view          : .comm
//   INCLUDE "file"     : .main only
//
// Everything that opens, saves, filters, drops or restores files goes through
// these helpers so the rule is defined once.

#include "uSharedScriptExt.hpp"

#include <QString>

namespace FileTypes
{
    inline bool isMain(const QString &path) { return scriptext::isMainScript(path.toStdString()); }
    inline bool isComm(const QString &path) { return scriptext::isCommScript(path.toStdString()); }
    inline bool isIni(const QString &path) { return scriptext::isIniFile(path.toStdString()); }
    inline bool isShell(const QString &path) { return scriptext::isShellScript(path.toStdString()); }
    inline bool isMainEditorFile(const QString &path) { return scriptext::isMainEditorFile(path.toStdString()); }

    // Dialog filters. Deliberately no "All files" entry: other types must not be selectable.
    inline QString mainEditorFilterFor(bool bWindows)
    {
        return bWindows ? QStringLiteral("Script files (*.main *.ini *.bat *.cmd *.ps1)")
                        : QStringLiteral("Script files (*.main *.ini *.sh)");
    }
    inline QString mainEditorFilter() { return mainEditorFilterFor(scriptext::kHostIsWindows); }
    inline QString mainOnlyFilter() { return QStringLiteral("Main scripts (*.main)"); }
    inline QString commFilter() { return QStringLiteral("Comm scripts (*.comm)"); }

    // Human-readable lists for messages.
    inline QString mainEditorListFor(bool bWindows)
    {
        return bWindows ? QStringLiteral(".main, .ini, .bat, .cmd, .ps1") : QStringLiteral(".main, .ini, .sh");
    }
    inline QString mainEditorList() { return mainEditorListFor(scriptext::kHostIsWindows); }
} // namespace FileTypes

#endif // FILE_TYPES_HPP
