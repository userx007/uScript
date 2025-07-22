#ifndef PLUGINSCRIPTINTERPRETER_HPP
#define PLUGINSCRIPTINTERPRETER_HPP

#include "CommonSettings.hpp"
#include "IScriptInterpreter.hpp"
#include "PluginScriptDataTypes.hpp"

#include "uLogger.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uNumeric.hpp"
#include "uTimer.hpp"

#include <regex>
#include <string>

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

        explicit PluginScriptInterpreter (PFSEND pfsend, PFRECV pfrecv, size_t szDelay, size_t szMaxRecvSize)
            : m_pfsend(pfsend)
            , m_pfrecv(pfrecv)
            , m_szDelay(szDelay)
            , m_szMaxRecvSize(szMaxRecvSize)
            {}

        bool interpretScript (PluginScriptEntriesType& sScriptEntries) override
        {
            bool bRetVal = false;

            for (const auto& item : sScriptEntries.vCommands) {
                LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Executing: ");LOG_STRING(getDirName(item.direction)); LOG_STRING("["); LOG_STRING(item.values.first); LOG_STRING(":"); LOG_STRING(item.values.second); LOG_STRING("] => ["); LOG_STRING(getTokenName(item.tokens.first)); LOG_STRING(":"); LOG_STRING(getTokenName(item.tokens.second)); LOG_STRING("]"));

                if (false == (bRetVal = (Direction::SEND_RECV == item.direction)  ? (m_handleSendAny(item) && m_handleRecvAny(item))
                                                                                  : (m_handleRecvAny(item) && m_handleSendAny(item)))) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script item execution failed"));
                    break;
                }

                /* delay between commands execution */
                utime::delay_ms(m_szDelay);
            }

            LOG_PRINT(((true == bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("->"); LOG_STRING((true == bRetVal) ? "OK" : "FAILED"));
            return bRetVal;

        } /* interpretScript() */

    private:

        bool m_getData (const std::string& input, TokenType tokenType, std::vector<uint8_t>& vData)
        {
            bool bRetVal = false;

            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("input/token"); LOG_STRING(input); LOG_STRING(getTokenName(tokenType)));

            switch (tokenType)
            {
                case TokenType::HEXSTREAM: {
                        bRetVal = hexutils::hexstringToVector(input, vData);
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("hexstringToVector ->"); LOG_BOOL(bRetVal));
                        break;
                    }

                case TokenType::LINE: {
                        bRetVal = ustring::stringToVector(input, vData);
                        ustring::replaceNullWithNewline(vData); // insert a new line in vector
                        break;
                    }

                case TokenType::TOKEN:
                case TokenType::STRING_RAW:
                case TokenType::STRING_DELIMITED:
                case TokenType::STRING_DELIMITED_EMPTY: {
                        bRetVal = ustring::stringToVector(input, vData);
                        break;
                    }

                default:{
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Token is incompatible with data"));
                        break;
                    }
            }

            return bRetVal;

        } /* m_getData() */


        bool m_handleRecvAny (PToken item)
        {
            bool bRetVal = false;

            if(!m_pfrecv) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("m_pfrecv callback not provided!"));
                return bRetVal;
            }

            /* wait for data to be received
               Note: invalid cases must have been rejected already by the item validator
            */
            TokenType tokenType = (Direction::RECV_SEND == item.direction) ? item.tokens.first : item.tokens.second;
            switch (tokenType)
            {
                case TokenType::EMPTY: {
                    bRetVal = true;
                    break;
                }

                case TokenType::REGEX: {
                    std::vector<uint8_t> vDataReceived(m_szMaxRecvSize);
                    size_t szReceived = 0;

                    if (m_pfrecv(vDataReceived, szReceived, ReadType::DEFAULT)) {                  /* receive data first to avoid delays */
                        std::string strReceived(vDataReceived.begin(), vDataReceived.end());       /* convert the received data to a string */
                        bRetVal = m_matchesPattern(strReceived, (Direction::RECV_SEND == item.direction) ? item.values.first : item.values.second); /* try to match the received data with the pattern */
                    }
                    break;
                }

                case TokenType::TOKEN: {
                    std::vector<uint8_t> vDataExpected{};


                    if(m_getData((Direction::RECV_SEND == item.direction) ? item.values.first : item.values.second,
                                 (Direction::RECV_SEND == item.direction) ? item.tokens.first : item.tokens.second,
                                 vDataExpected))
                    {
                        size_t szExpected = vDataExpected.size();
                        bRetVal = m_pfrecv(vDataExpected, szExpected, ReadType::TOKEN);               /* wait for the specified token                  */
                    }
                    break;
                }


                /* TokenType::STRING_DELIMITED, STRING_DELIMITED_EMPTY, STRING_RAW, LINE */
                default: {
                    std::vector<uint8_t> vDataExpected{}; // buffer where the expected data is converted to a vector
                    std::vector<uint8_t> vDataReceived(m_szMaxRecvSize);
                    size_t szReceived = 0;

                    ReadType readType = ((TokenType::TOKEN == tokenType) ? ReadType::TOKEN : ((TokenType::LINE == tokenType) ? ReadType::LINE : ReadType::DEFAULT));
                    bRetVal = (    m_pfrecv(vDataReceived, szReceived, readType)                   /* first receive the data to avoid delays */
                                && m_getData((Direction::RECV_SEND == item.direction) ? item.values.first : item.values.second,
                                             (Direction::RECV_SEND == item.direction) ? item.tokens.first : item.tokens.second,
                                             vDataExpected)                                        /* convert the data to be expected */
                                && numeric::compareVectors<uint8_t>(vDataReceived, vDataExpected, szReceived)); /* evaluate the received vs. expected data */
                    break;
                }
            }

            LOG_PRINT( ((true ==bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING((Direction::RECV_SEND == item.direction) ? item.values.first : item.values.second); LOG_STRING("|");
                                                                              LOG_STRING(getTokenName((Direction::RECV_SEND == item.direction) ? item.tokens.first : item.tokens.second));
                                                                              LOG_STRING(((true ==bRetVal) ? "ok" : "failed")) );
            return bRetVal;

        } /* m_handleRecvAny() */


        bool m_handleSendAny (PToken item)
        {
            bool bRetVal = false;

            /* check if a callback was provided */
            if(!m_pfsend) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("m_pfsend callback not provided!"));
                return bRetVal;
            }

            /* send data
               Note: invalid cases must have been rejected already by the item validator
            */
            switch((Direction::SEND_RECV == item.direction) ? item.tokens.first : item.tokens.second)
            {
                case TokenType::EMPTY: {
                    bRetVal = true;
                    break;
                }

                /* send a file */
                case TokenType::FILENAME:{
                    bRetVal = m_handleSendFile(item);
                    break;
                }

                /* send a stream (hex, string, etc.) */
                default: {
                    bRetVal = m_handleSendStream(item);
                    break;
                }
            }

            return bRetVal;

        } /* m_handleSendAny() */


        bool m_handleSendStream (PToken item)
        {
            std::vector<uint8_t> vData;
            return m_getData(((Direction::SEND_RECV == item.direction) ? item.values.first : item.values.second),
                             ((Direction::SEND_RECV == item.direction) ? item.tokens.first : item.tokens.second), vData)
                && m_pfsend(vData);

        } /* m_handleSendStream() */


        bool m_handleSendFile (PToken item)
        {
            bool bRetVal = false;

            do {
                /* extract the filepathname and optionally the chunksize
                   Note: a file can only be sent therefore it must only be the first token,
                   otherwise the invalid case must have been rejected already by the item validator
                */
                std::pair<std::string, std::string> result;
                ustring::splitAtFirst(((Direction::SEND_RECV == item.direction) ? item.values.first : item.values.second), CHAR_SEPARATOR_COMMA, result);

                /* get the filesize */
                std::uintmax_t fileSize = ufile::getFileSize(result.first);
                size_t chunkSize = PLUGIN_DEFAULT_FILEREAD_CHUNKSIZE;

                /* if a chunksize was also provided as argumen then convert it to a number */
                if (false == result.second.empty()) {
                   if (false == numeric::str2size_t(result.second, chunkSize)) {
                        break;
                   }
                }

                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(result.first); LOG_STRING("-> Size:"); LOG_UINT64(fileSize); LOG_STRING("ChunkSize:"); LOG_UINT64(chunkSize));

                /* transfter the file */
                if (false == ufile::FileChunkReader::read(result.first, chunkSize, m_pfsend)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(result.first); LOG_STRING(": failed to read in chunks"));
                    break;
                }

                bRetVal = true;

            } while(false);

            return bRetVal;

        } /* m_handleSendFile() */


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


        size_t m_szDelay;
        size_t m_szMaxRecvSize;
        PFSEND m_pfsend;
        PFRECV m_pfrecv;
};

#endif //PLUGINSCRIPTINTERPRETER_HPP