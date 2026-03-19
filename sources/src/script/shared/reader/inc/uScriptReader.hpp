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
 *
 * Each entry in the output vector carries the 1-based line number from the
 * original file so that downstream components (validator, frontend) can map
 * every compiled IR node back to its exact source location.
 */
class ScriptReader : public IScriptReader
{
public:

    explicit ScriptReader(const std::string& strScriptPathName)
        : m_strScriptPathName(strScriptPathName)
    {}

    bool readScript(std::vector<ScriptRawLine>& vRawLines) override
    {
        bool bRetVal = false;
        char strLineNumber[16];

        std::ifstream file(m_strScriptPathName);

        if (file.is_open()) {

            std::string strLine;
            bool bIgnoreLines = false;
            int  iLineNumber  = 0;         

            while( std::getline(file, strLine)) {

                ++iLineNumber;
                std::snprintf(strLineNumber, sizeof(strLineNumber), "%03d:", iLineNumber);

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
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
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
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
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

                vRawLines.push_back({iLineNumber, strContent});

            } // while( std::getline(file, strLine))

            file.close();
            bRetVal = true;

        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Unable to open file:"); 
                      LOG_STRING(m_strScriptPathName));
            bRetVal = false;
        }

        if (true == bRetVal) {
            for (const auto & rawLine : vRawLines) {
                std::snprintf(strLineNumber, sizeof(strLineNumber), "%03d:", rawLine.iLineNumber);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING(rawLine.strContent));
            }
        }

        return bRetVal;
    }

private:

    std::string m_strScriptPathName;

};

#endif // U_SCRIPT_READER_HPP
