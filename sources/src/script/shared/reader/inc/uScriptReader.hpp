#ifndef U_SCRIPT_READER_HPP
#define U_SCRIPT_READER_HPP

#include "IScriptReader.hpp"
#include "uSharedConfig.hpp"
#include "uScriptDataTypes.hpp"
#include "uString.hpp"
#include "uLogger.hpp"

#include <vector>
#include <string>
#include <fstream>
#include <utility>
#include <filesystem>
#include <unordered_set>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "CORE_SCR_R  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                    CLASS DECLARATION / DEFINITION                           //
/////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Header-only implementation of script reader
 *
 * Reads script files with support for:
 * - Line comments (starting with SCRIPT_LINE_COMMENT)
 * - Block comments (SCRIPT_BEGIN_BLOCK_COMMENT to SCRIPT_END_BLOCK_COMMENT)
 * - Inline comments (comment at end of line)
 * - Automatic trimming of whitespace
 * - Line continuation: a trailing backslash joins the next physical line
 * - INCLUDE "file" directives:
 *     The named file is resolved relative to the directory of the including
 *     file and processed recursively through the same reader (so comments,
 *     block-comments, line continuation and nested INCLUDEs all work).
 *     Circular or duplicate includes are detected and reported as errors.
 *     Included content is prepended ahead of the host file's own lines so
 *     constants declared in an included file are available before the host
 *     script's body is parsed, regardless of where the INCLUDE line sits.
 *
 * Each entry in the output vector carries the 1-based line number from its
 * origin file so that downstream components (validator, GUI) can map every
 * compiled IR node back to its exact source location.
 *
 * The INCLUDE keyword is defined in uSharedConfig.hpp as
 * SCRIPT_INCLUDE_KEYWORD so the GUI highlighter can reference the same
 * string constant without a separate header dependency.
 */
class ScriptReader : public IScriptReader
{
public:

    explicit ScriptReader(const std::string& strScriptPathName)
        : m_strScriptPathName(strScriptPathName)
    {}

    bool readScript(std::vector<ScriptRawLine>& vRawLines) override
    {
        std::unordered_set<std::string> setVisitedPaths;
        return readScriptFile(m_strScriptPathName, vRawLines, setVisitedPaths);
    }

private:

    // -------------------------------------------------------------------------
    // Resolve an INCLUDE target: relative paths are anchored to the directory
    // of the file that contains the INCLUDE, not the process working directory.
    // -------------------------------------------------------------------------
    static std::string resolveIncludePath(const std::string& strIncludingFile,
                                          const std::string& strIncludePath)
    {
        std::filesystem::path inc(strIncludePath);
        if (inc.is_absolute()) return inc.string();
        return (std::filesystem::path(strIncludingFile).parent_path() / inc).string();
    }

    // -------------------------------------------------------------------------
    // Test whether a (already-stripped) content line is an INCLUDE directive.
    // Format:   INCLUDE "relative/or/absolute/path"
    // Returns true and fills strOutPath on a match; false otherwise.
    // Uses a simple manual parse — avoids a <regex> dependency in the reader.
    // -------------------------------------------------------------------------
    static bool matchIncludeDirective(const std::string& strContent,
                                       std::string& strOutPath)
    {
        // Must start with the keyword followed by whitespace
        const std::string kw(SCRIPT_INCLUDE_KEYWORD);
        if (strContent.size() <= kw.size()) return false;
        if (strContent.compare(0, kw.size(), kw) != 0) return false;
        if (!std::isspace(static_cast<unsigned char>(strContent[kw.size()]))) return false;

        // Skip whitespace between keyword and opening quote
        size_t pos = kw.size();
        while (pos < strContent.size() &&
               std::isspace(static_cast<unsigned char>(strContent[pos])))
            ++pos;

        // Must be a double-quoted path
        if (pos >= strContent.size() || strContent[pos] != '"') return false;
        ++pos; // skip opening quote

        const size_t pathStart = pos;
        while (pos < strContent.size() && strContent[pos] != '"') ++pos;
        if (pos >= strContent.size()) return false; // unterminated quote

        // Trailing characters after the closing quote must be only whitespace
        const size_t closeQuote = pos;
        ++pos; // skip closing quote
        while (pos < strContent.size() &&
               std::isspace(static_cast<unsigned char>(strContent[pos])))
            ++pos;
        if (pos != strContent.size()) return false; // junk after closing quote

        strOutPath = strContent.substr(pathStart, closeQuote - pathStart);
        return !strOutPath.empty();
    }

    // -------------------------------------------------------------------------
    // Read one file recursively, expanding INCLUDE directives as they appear.
    //
    // Design: included content is collected into vIncludedLines, this file's
    // own content into vOwnLines.  On return vIncludedLines is prepended to
    // vOwnLines so that constants declared in an included file are visible
    // before this file's body regardless of where the INCLUDE physically sits.
    // -------------------------------------------------------------------------
    bool readScriptFile(const std::string& strPathName,
                         std::vector<ScriptRawLine>& vRawLines,
                         std::unordered_set<std::string>& setVisitedPaths)
    {
        // Canonicalise for the cycle-detection set — weakly_canonical tolerates
        // paths that do not yet exist, but the file check below will catch that.
        std::error_code ec;
        std::filesystem::path canonical =
            std::filesystem::weakly_canonical(strPathName, ec);
        const std::string strKey = (!ec) ? canonical.string() : strPathName;

        if (!setVisitedPaths.insert(strKey).second) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Circular or duplicate INCLUDE detected for file:");
                      LOG_STRING(strPathName));
            return false;
        }

        std::ifstream file(strPathName);

        if (!file.is_open()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Unable to open file:");
                      LOG_STRING(strPathName));
            return false;
        }

        std::vector<ScriptRawLine> vIncludedLines; // lines from INCLUDE'd files
        std::vector<ScriptRawLine> vOwnLines;      // this file's own lines

        std::string strLine;
        bool bIgnoreLines = false;
        int  iLineNumber  = 0;

        while (std::getline(file, strLine)) {

            ++iLineNumber;
            auto lineNr = ustring::fmtLineNr(iLineNumber);

            ustring::trimInPlace(strLine);

            // Skip empty lines and full-line comments
            if (strLine.empty() || (SCRIPT_LINE_COMMENT == strLine.at(0))) {
                continue;
            }

            // Block comment: opening delimiter
            if (0 == strLine.compare(SCRIPT_BEGIN_BLOCK_COMMENT)) {
                if (!bIgnoreLines) {
                    bIgnoreLines = true;
                    continue;
                }
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING("Nested block comment not supported"));
                return false;
            }

            // Block comment: closing delimiter
            if (0 == strLine.compare(SCRIPT_END_BLOCK_COMMENT)) {
                if (bIgnoreLines) {
                    bIgnoreLines = false;
                    continue;
                }
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING("Invalid end of block comment"));
                return false;
            }

            if (bIgnoreLines) continue;

            // Strip trailing inline comment
            std::pair<std::string, std::string> strSplitLine;
            ustring::splitAtFirst(strLine, SCRIPT_LINE_COMMENT, strSplitLine);
            std::string strContent = (!strSplitLine.second.empty())
                                         ? strSplitLine.first
                                         : strLine;

            // Line continuation: trailing backslash joins the next line(s)
            ustring::trimInPlace(strContent);
            while (!strContent.empty() && strContent.back() == '\\') {
                strContent.pop_back();
                ustring::trimInPlace(strContent);

                std::string strNextLine;
                if (!std::getline(file, strNextLine)) break;
                ++iLineNumber;
                ustring::trimInPlace(strNextLine);

                std::pair<std::string, std::string> strNextSplit;
                ustring::splitAtFirst(strNextLine, SCRIPT_LINE_COMMENT, strNextSplit);
                std::string strNextContent = (!strNextSplit.second.empty())
                                                 ? strNextSplit.first
                                                 : strNextLine;
                ustring::trimInPlace(strNextContent);
                strContent += strNextContent;
                ustring::trimInPlace(strContent);
            }

            // INCLUDE "path" — resolve and recurse
            std::string strIncludeTarget;
            if (matchIncludeDirective(strContent, strIncludeTarget)) {
                const std::string strResolved =
                    resolveIncludePath(strPathName, strIncludeTarget);

                std::vector<ScriptRawLine> vNested;
                if (!readScriptFile(strResolved, vNested, setVisitedPaths)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                              LOG_STRING("Failed to process INCLUDE file:");
                              LOG_STRING(strResolved));
                    return false;
                }
                vIncludedLines.insert(vIncludedLines.end(),
                                      vNested.begin(), vNested.end());
                continue;
            }

            vOwnLines.push_back({iLineNumber, strContent});

        } // while getline

        // Included content precedes this file's own content
        vRawLines.insert(vRawLines.end(), vIncludedLines.begin(), vIncludedLines.end());
        vRawLines.insert(vRawLines.end(), vOwnLines.begin(), vOwnLines.end());

        if (!vRawLines.empty()) {
            for (const auto& rawLine : vRawLines) {
                auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING(rawLine.strContent));
            }
        }

        return true;
    }

    std::string m_strScriptPathName;
};

#endif // U_SCRIPT_READER_HPP
