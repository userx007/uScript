#include "ScriptReader.hpp"
#include "CommonSettings.hpp"
#include "uString.hpp"
#include "uLogger.hpp"


#include <vector>
#include <string>
#include <fstream>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "S_READER   :"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                            PUBLIC INTERFACES                                //
/////////////////////////////////////////////////////////////////////////////////


bool ScriptReader::readScript(std::vector<std::string>& vstrScriptLines)
{
    bool bRetVal = false;

    std::ifstream file(m_strScriptPathName);

    if (file.is_open()) {

        std::string strLine;
        bool bIgnoreLines = false;

        while( std::getline(file, strLine)) {

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
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Nested block comment not supported"));
                    break;
                }
            }

            // end skipping lines in a block-comment
            if (0 == strLine.compare(SCRIPT_END_BLOCK_COMMENT)) {
                if (true == bIgnoreLines ) {
                    bIgnoreLines = false;
                    continue;
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid end of block comment"));
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
            //the command and removed comment and store the command
            if (false == (strSplitLine.second).empty()) {
                vstrScriptLines.push_back(strSplitLine.first);
            } else {
                vstrScriptLines.push_back(strLine);
            }
        } // while( std::getline(file, strLine))

        file.close();
        bRetVal = true;

    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unable to open file:"); LOG_STRING(m_strScriptPathName));
        bRetVal = false;
    }

    if (true == bRetVal) {
        for (const auto & line : vstrScriptLines) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(line));
        }
    }

    return bRetVal;

}
