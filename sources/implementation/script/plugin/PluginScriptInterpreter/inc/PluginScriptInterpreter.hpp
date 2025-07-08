#ifndef PLUGINSCRIPTINTERPRETER_HPP
#define PLUGINSCRIPTINTERPRETER_HPP

#include "CommonSettings.hpp"
#include "IScriptInterpreter.hpp"
#include "PluginScriptDataTypes.hpp"

#include "uLogger.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uNumeric.hpp"

#include <regex>
#include <string>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

static constexpr size_t szDefaulChunkSize = 1024;

/////////////////////////////////////////////////////////////////////////////////
//                             LOG DEFINITIONS                                 //
/////////////////////////////////////////////////////////////////////////////////


#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "PSINTERPRET:"
#define LOG_HDR    LOG_STRING(LT_HDR); LOG_STRING(__FUNCTION__)

/////////////////////////////////////////////////////////////////////////////////
//                            CLASS DEFINITION                                 //
/////////////////////////////////////////////////////////////////////////////////

class PluginScriptInterpreter : public IScriptInterpreter<PluginScriptEntriesType>
{

    public:

        PluginScriptInterpreter () = default;
        virtual ~PluginScriptInterpreter () = default;

        bool interpretScript(PluginScriptEntriesType& sScriptEntries, PFSEND pfsend, PFRECV pfrecv ) override
        {
            bool bRetVal = false;
            std::vector<uint8_t> vSendData;
            std::vector<uint8_t> vRecvData;

            for (const auto& item : sScriptEntries.vCommands)
            {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Executing ["); LOG_STRING(item.first.first); LOG_STRING("|"); LOG_STRING(item.first.second); LOG_STRING("] -> ["); LOG_STRING(getTokenName(item.second.first)); LOG_STRING("|"); LOG_STRING(getTokenName(item.second.second)); LOG_STRING("]"));

                // dispatch the send type, the receive type is handled
                switch (item.second.first)
                {
                    /* nothing to send, only receive something */
                    case TokenType::EMPTY:{
                        bRetVal = m_handleRecvAny(item, pfrecv);
                        break;
                    }

                    /* send a file */
                    case TokenType::FILENAME:{
                        bRetVal = m_handleSendFileRecvAny(item, pfsend, pfrecv);
                        break;
                    }

                    /* send a stream (hex, string, etc.) */
                    default: {
                        bRetVal = m_handleSendStreamRecvAny(item, pfsend, pfrecv);
                        break;
                    }
                }

                /* evaluate the execution status */
                if (false == bRetVal)
                {
                    break; /* stop the for(...) loop */
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("command execution failed"));
                }
            }

            LOG_PRINT(((true == bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("->"); LOG_STRING((true == bRetVal) ? "OK" : "FAILED"));

            return bRetVal;

        } /* interpretScript() */

    private:

        bool m_getData(const std::string& input, TokenType tokenType, std::vector<uint8_t>& vData)
        {
            bool bRetVal = true;

            switch (tokenType)
            {
                case TokenType::HEXSTREAM: {
                        bRetVal = hexutils::hexstringToVector(input, vData);
                        break;
                    }

                case TokenType::STRING_RAW:
                case TokenType::STRING_DELIMITED:
                case TokenType::STRING_DELIMITED_EMPTY: {
                        ustring::stringToVector(input, vData);
                        break;
                    }

                default:{
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Token is incompatible with data"));
                        bRetVal = false;
                        break;
                    }
            }

            return bRetVal;

        } /* m_getData() */


        bool m_handleSendStream (PCommandType cmd, PFSEND pfsend)
        {
            std::vector<uint8_t> vData;
            return (m_getData(cmd.first.first, cmd.second.first, vData) && pfsend(vData));

        } /* m_handleSendStream() */


        bool m_handleSendFile (PCommandType cmd, PFSEND pfsend)
        {
            bool bRetVal = false;
            std::string strOutput;

            do {
                if (false == ustring::undecorate(cmd.first.first, DECORATOR_FILENAME_START, DECORATOR_ANY_END, strOutput)) {
                    break;
                }

                /* extract the filepathname and optionally the chunksize */
                std::pair<std::string, std::string> result;
                ustring::splitAtFirst(strOutput, CHAR_SEPARATOR_COMMA, result);

                /* get the filesize */
                std::uintmax_t fileSize = ufile::getFileSize(result.first);
                size_t chunkSize = szDefaulChunkSize;

                /* if a chunksize was also provided, convert it to a number */
                if (false == result.second.empty()) {
                   if (false == numeric::str2size_t(result.second, chunkSize)) {
                        break;
                   }
                }

                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(result.first); LOG_STRING("-> Size:"); LOG_UINT64(fileSize); LOG_STRING("ChunkSize:"); LOG_UINT64(chunkSize));

                /* transfter the file */
                if (false == ufile::FileChunkReader::read(strOutput, chunkSize, pfsend)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strOutput); LOG_STRING(": Failed to read in chunks"));
                    break;
                }

                bRetVal = true;

            } while(false);

            return bRetVal;

        } /* m_handleSendFile() */


        bool m_handleSendStreamRecvAny (PCommandType cmd, PFSEND pfsend, PFRECV pfrecv)
        {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("recv:"); LOG_STRING(getTokenName(cmd.second.second)));

            return (m_handleSendStream(cmd, pfsend) && m_handleRecvAny(cmd, pfrecv));

        } /* m_handleSendStreamRecvAny() */


        bool m_handleSendFileRecvAny (PCommandType cmd, PFSEND pfsend, PFRECV pfrecv)
        {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("recv:"); LOG_STRING(getTokenName(cmd.second.second)));

            return (m_handleSendFile(cmd, pfsend) && m_handleRecvAny(cmd, pfrecv));

        } /* m_handleSendFileRecvAny() */


        bool m_handleRecvAny (PCommandType cmd, PFRECV pfrecv)
        {
            bool bRetVal = false;

            /* wait for data to be received */
            switch (cmd.second.second)
            {

                case TokenType::EMPTY: {
                    bRetVal = true;                                                                /* nothing to receive, just return */
                    break;
                }

                case TokenType::REGEX: {
                    std::vector<uint8_t> vDataReceived;
                    if (pfrecv(vDataReceived)) {                                                   /* receive data first to avoid delays              */
                        std::string strReceived(vDataReceived.begin(), vDataReceived.end());       /* convert the received data to a string           */
                        bRetVal = m_matchesPattern(strReceived, cmd.first.second);                 /* try to match the received data with the pattern */
                    }
                    break;
                }

                /* TokenType::HEXSTREAM, STRING_DELIMITED, STRING_DELIMITED_EMPTY, STRING_RAW */
                default: {
                    std::vector<uint8_t> vDataExpected;
                    std::vector<uint8_t> vDataReceived;

                    bRetVal = ( pfrecv(vDataReceived) &&                                           /* first receive the data to avoid delays  */
                                m_getData(cmd.first.second, cmd.second.second, vDataExpected) &&   /* prepare the expected data               */
                                (vDataExpected == vDataReceived) );                                /* evaluate the received vs. expected data */
                    break;
                }
            }

            LOG_PRINT( ((true ==bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(cmd.first.second); LOG_STRING("|"); LOG_STRING(getTokenName(cmd.second.second)); LOG_STRING(((true ==bRetVal) ? "ok" : "failed")) );
            return bRetVal;

        } /* m_handleRecvAny() */


        bool m_matchesPattern (const std::string& input, const std::string& patternStr)
        {
            try {
                std::regex pattern(patternStr);
                return std::regex_match(input, pattern);
            } catch (const std::regex_error& e) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid regex pattern:"); LOG_STRING(patternStr));
                return false;
            }
        } /* m_matchesPattern() */

};

#endif // PLUGINSCRIPTINTERPRETER_HPP

