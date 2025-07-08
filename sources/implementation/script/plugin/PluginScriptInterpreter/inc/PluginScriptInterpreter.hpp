#ifndef PLUGINSCRIPTINTERPRETER_HPP
#define PLUGINSCRIPTINTERPRETER_HPP

#include "CommonSettings.hpp"
#include "IScriptInterpreter.hpp"
#include "PluginScriptDataTypes.hpp"

#include "uLogger.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
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
                    // nothing to send, only receive something
                    case TokenType::EMPTY:{
                        bRetVal = m_handleRecvAny(item, pfrecv);
                        break;
                    }

                    // send a file
                    case TokenType::FILENAME:{
                        bRetVal = m_handleSendFileRecvAny(item, pfsend, pfrecv);
                        break;
                    }

                    // send a stream (hex, string, etc.)
                    default: {
                        bRetVal = m_handleSendStreamRecvAny(item, pfsend, pfrecv);
                        break;
                    }
                }

                // evaluate the execution status
                if (false == bRetVal)
                {
                    break; // stop the for(...) loop
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("command execution failed"));
                }
            }

            LOG_PRINT(((true == bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("->"); LOG_STRING((true == bRetVal) ? "OK" : "FAILED"));

            return bRetVal;

        } /* interpretScript() */

    private:

        bool m_getData(const std::string& input, TokenType tokenType, std::vector<uint8_t> vData)
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
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(__FUNCTION__));

            return true;

        } /* m_handleSendStream() */



        bool m_handleSendFile (PCommandType cmd, PFSEND pfsend)
        {
            std::string strFilePathName;
            if (true == ustring::undecorate(cmd.first.first, DECORATOR_FILENAME_START, DECORATOR_ANY_END, strFilePathName)) {
                std::uintmax_t size = ufile::getFileSize(strFilePathName);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strFilePathName); LOG_STRING("Size:"); LOG_UINT64(size));

                // transfter the file
                const std::size_t chunkSize = 8192;

                if (false == ufile::FileChunkReader::read(strFilePathName, chunkSize, pfsend)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strFilePathName); LOG_STRING(": Failed to read in chunks"));
                }
            }

            return true;

        } /* m_handleSendFile() */


        bool m_handleSendStreamRecvAny (PCommandType cmd, PFSEND pfsend, PFRECV pfrecv)
        {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("recv:"); LOG_STRING(getTokenName(cmd.second.second)));

            return m_handleSendStream(cmd, pfsend) && m_handleRecvAny(cmd, pfrecv);

        } /* m_handleSendStreamRecvAny() */


        bool m_handleSendFileRecvAny (PCommandType cmd, PFSEND pfsend, PFRECV pfrecv)
        {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("recv:"); LOG_STRING(getTokenName(cmd.second.second)));

            return m_handleSendFile(cmd, pfsend) && m_handleRecvAny(cmd, pfrecv);

        } /* m_handleSendFileRecvAny() */


        bool m_handleRecvAny (PCommandType cmd, PFRECV pfrecv)
        {
            bool bRetVal = false;
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(getTokenName(cmd.second.second)));

            // wait for data to be received
            switch (cmd.second.second)
            {
                // nothing to receive, just return
                case TokenType::EMPTY: {
                    bRetVal = true;
                    break;
                }

                case TokenType::REGEX: {
                    // handle regex
                    bRetVal = true;
                    break;
                }

                case TokenType::HEXSTREAM: {
                    // handle hexstream
                    bRetVal = true;
                    break;
                }

                default: {
                    // handle strings
                    bRetVal = true;
                    break;
                }
            }

            return bRetVal;

        } /* m_handleRecvAny() */
};

#endif // PLUGINSCRIPTINTERPRETER_HPP

