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
#include <regex>
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

// INCLUDE "path/to/file"
// Standalone directive recognised by the reader (never reaches the IR):
// keyword followed by the path of the file to include, enclosed in double
// quotes. A relative path is resolved against the directory of the file
// that contains the INCLUDE statement (not the process' current directory),
// so includes work regardless of where the tool is invoked from.
#define SCRIPT_INCLUDE_KEYWORD    "INCLUDE"


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
 * - INCLUDE "file" directives: the named file is read by a separate
 *   ScriptReader instance (so it goes through the very same comment /
 *   block-comment / continuation handling as the core script) and its
 *   resulting lines are prepended ahead of every line belonging to the
 *   core script. This guarantees constant macros declared in an included
 *   file are available before the core script's content is parsed/expanded,
 *   regardless of where in the core script the INCLUDE directive appears.
 *
 * Each entry in the output vector carries the 1-based line number from the
 * original file so that downstream components (validator, frontend) can map
 * every compiled IR node back to its exact source location. Lines coming
 * from an included file keep the line number from within that included
 * file, not from the core script.
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

    // ---------------------------------------------------------------------
    // Resolves an INCLUDE target path: absolute paths are used as-is,
    // relative paths are resolved against the directory of the file that
    // contains the INCLUDE statement (not the process' current directory).
    // ---------------------------------------------------------------------
    static std::string resolveIncludePath(const std::string& strIncludingFile,
                                           const std::string& strIncludePath)
    {
        std::filesystem::path includePath(strIncludePath);

        if (includePath.is_absolute()) {
            return includePath.string();
        }

        std::filesystem::path baseDir = std::filesystem::path(strIncludingFile).parent_path();
        return (baseDir / includePath).string();
    }

    // ---------------------------------------------------------------------
    // Recognises a standalone  INCLUDE "path"  directive.
    // Returns true and fills strOutPath on a match.
    // ---------------------------------------------------------------------
    static bool matchIncludeDirective(const std::string& strContent, std::string& strOutPath)
    {
        static const std::regex pattern(
            "^" SCRIPT_INCLUDE_KEYWORD "\\s+\"([^\"]+)\"\\s*$");

        std::smatch match;
        if (std::regex_match(strContent, match, pattern)) {
            strOutPath = match[1].str();
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------------
    // Reads a single script file (core script or an included one),
    // resolving any INCLUDE directives it contains along the way.
    //
    // The content of every included file is collected separately and
    // placed ahead of the file's own lines, so that — no matter where in
    // the file the INCLUDE statement physically sits — included content
    // is always available before the file's own content is processed
    // (mandatory for constant macros to be expanded correctly later on).
    // ---------------------------------------------------------------------
    bool readScriptFile(const std::string& strPathName,
                         std::vector<ScriptRawLine>& vRawLines,
                         std::unordered_set<std::string>& setVisitedPaths)
    {
        bool bRetVal = false;

        // detect circular / repeated includes
        std::error_code ec;
        std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(strPathName, ec);
        std::string strCanonicalKey = (!ec) ? canonicalPath.string() : strPathName;

        if (false == setVisitedPaths.insert(strCanonicalKey).second) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Circular or duplicate INCLUDE detected for file:");
                      LOG_STRING(strPathName));
            return false;
        }

        std::ifstream file(strPathName);

        if (file.is_open()) {

            std::vector<ScriptRawLine> vIncludedLines; // content brought in via INCLUDE
            std::vector<ScriptRawLine> vOwnLines;      // this file's own content

            std::string strLine;
            bool bIgnoreLines = false;
            int  iLineNumber  = 0;         

            while( std::getline(file, strLine)) {

                ++iLineNumber;
                auto lineNr = ustring::fmtLineNr(iLineNumber);

                // remove the leading and trailing spaces
                ustring::trimInPlace(strLine);

                // ignore the empty or commented-out lines
                if (strLine.empty() || (SCRIPT_LINE_COMMENT == strLine.at(0))) {
                    continue;
                }

                // begin skipping lines in a block-comment
                if (0 == strLine.compare(SCRIPT_BEGIN_BLOCK_COMMENT)) {
                    if (false == bIgnoreLines ) {
                        bIgnoreLines = true;
                        continue;
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                                  LOG_STRING("Nested block comment not supported"));
                        break;
                    }
                }

                // end skipping lines in a block-comment
                if (0 == strLine.compare(SCRIPT_END_BLOCK_COMMENT)) {
                    if (true == bIgnoreLines ) {
                        bIgnoreLines = false;
                        continue;
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                                  LOG_STRING("Invalid end of block comment"));
                        break;
                    }
                }

                // skip lines inside a block-comment
                if (true == bIgnoreLines ) {
                    continue;
                }

                // remove the eventual comment at the end of line
                std::pair<std::string, std::string> strSplitLine;
                ustring::splitAtFirst(strLine, SCRIPT_LINE_COMMENT, strSplitLine);

                // depending if the line contained a comment remove the trailing spaces between
                // the command and removed comment and store the command together with its line number
                std::string strContent = (false == (strSplitLine.second).empty())
                                             ? strSplitLine.first
                                             : strLine;

                // line continuation: if content ends with '\' join the
                // next physical line(s) until no trailing '\' remains.
                // The logical line keeps the line number of the first
                // physical line in the continuation group.
                ustring::trimInPlace(strContent);
                while (!strContent.empty() && strContent.back() == '\\') {
                    strContent.pop_back();           // strip the backslash
                    ustring::trimInPlace(strContent); // trim trailing spaces

                    std::string strNextLine;
                    if (!std::getline(file, strNextLine)) {
                        break; // EOF — accept partial continuation
                    }
                    ++iLineNumber;
                    ustring::trimInPlace(strNextLine);

                    // strip inline comment from the continuation line
                    std::pair<std::string, std::string> strNextSplit;
                    ustring::splitAtFirst(strNextLine, SCRIPT_LINE_COMMENT, strNextSplit);
                    std::string strNextContent = (false == (strNextSplit.second).empty())
                                                     ? strNextSplit.first
                                                     : strNextLine;
                    ustring::trimInPlace(strNextContent);

                    strContent += strNextContent;
                    ustring::trimInPlace(strContent);
                }

                // INCLUDE "file" — resolve it now, via a fresh ScriptReader
                // pass over the included file, so it goes through the very
                // same comment / block-comment / continuation handling.
                std::string strIncludeTarget;
                if (matchIncludeDirective(strContent, strIncludeTarget)) {

                    std::string strResolvedPath = resolveIncludePath(strPathName, strIncludeTarget);

                    std::vector<ScriptRawLine> vNestedLines;
                    if (false == readScriptFile(strResolvedPath, vNestedLines, setVisitedPaths)) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                                  LOG_STRING("Failed to process INCLUDE file:");
                                  LOG_STRING(strResolvedPath));
                        file.close();
                        return false;
                    }

                    vIncludedLines.insert(vIncludedLines.end(),
                                          vNestedLines.begin(), vNestedLines.end());
                    continue;
                }

                vOwnLines.push_back({iLineNumber, strContent});

            } // while( std::getline(file, strLine))

            file.close();

            // included content always precedes this file's own content,
            // independent of where the INCLUDE directive physically sits
            vRawLines.insert(vRawLines.end(), vIncludedLines.begin(), vIncludedLines.end());
            vRawLines.insert(vRawLines.end(), vOwnLines.begin(), vOwnLines.end());

            bRetVal = true;

        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Unable to open file:"); 
                      LOG_STRING(strPathName));
            bRetVal = false;
        }

        if (true == bRetVal) {
            for (const auto & rawLine : vRawLines) {
                auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING(rawLine.strContent));
            }
        }

        return bRetVal;
    }

    std::string m_strScriptPathName;

};

#endif // U_SCRIPT_READER_HPP
