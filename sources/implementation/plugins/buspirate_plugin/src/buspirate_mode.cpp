/*
http://dangerousprototypes.com/docs/Bitbang
*/

#include "buspirate_plugin.hpp"

#include "uNumeric.hpp"
#include "uLogger.hpp"

///////////////////////////////////////////////////////////////////
//                 LOG DEFINES                                   //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "BP_MODE    :"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

bool BuspiratePlugin::m_handle_mode(const std::string &args) const
{
    bool bRetVal = false;
    bool bShowHelp = false;

    if( "help" == args) {
        bShowHelp = true;
        bRetVal   = true;
    } else {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Mode:"); LOG_STRING(args));

        ModesMap::const_iterator it = m_mapModes.find(args);
        if (it != m_mapModes.end()) {

            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Found mode:"); LOG_STRING(args));

            // request
            std::vector<uint8_t> request(it->second.iRepetition);
            std::fill(request.begin(), request.end(), it->second.iRequest);
            // answer
            std::string strExpect { it->second.strAnswer };

            if (0 == strExpect.compare("-")) {
                bRetVal = generic_uart_send_receive(request, numeric::byte2span(m_positive_response));
            } else {
                std::vector<uint8_t> answer(strExpect.begin(), strExpect.end());
                bRetVal = generic_uart_send_receive(request, answer);
            }

        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid mode:"); LOG_STRING(args));
            bShowHelp = true;
        }
    }

    if(true == bShowHelp){
        std::string strModeList;
        for( auto it : m_mapModes ){
            strModeList += it.first;
            strModeList += " ";
        }
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use:"); LOG_STRING(strModeList.c_str()));
    }

    return bRetVal;

}
