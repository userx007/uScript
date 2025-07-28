/*
http://dangerousprototypes.com/docs/Bitbang
*/

#include "buspirate_plugin.hpp"

///////////////////////////////////////////////////////////////////
//                 DLT DEFINES                                   //
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
        typename ModesMap::const_iterator it = m_mapModes.find(args);

        if( it != m_mapModes.end() ) {
            std::vector<uint8_t> request(it->second.iRepetition, it->second.iRequest);
            std::string expect(it->second.pstrAnswer);

            if( 0 == expect.compare("-") ) {
                bRetVal = generic_uart_send_receive(request, g_positive_answer);
            } else {
                std::vector<uint8_t> answer(expect.begin(), expect.end());
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
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Try:"); LOG_STRING(strModeList.c_str()));
    }

    return bRetVal;

}
