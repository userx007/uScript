#include "uart_plugin.hpp"
#include "plugin_extension.hpp"
#include "string_handling.hpp"
#include "upload_handling.hpp"
#include "uart_handling.hpp"
#include "file_handling.h"
#include "script_parser.hpp"


#include "uNumeric.hpp"

///////////////////////////////////////////////////////////////////
//                 DLT DEFINES                                   //
///////////////////////////////////////////////////////////////////

#undef  DLT_HDR
#define DLT_HDR     DLT_STRING("UARTP    :");

DLT_DECLARE_CONTEXT(UartPluginCtx)

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////


/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED UARTPlugin* pluginEntry()
    {
        return new UARTPlugin();
    }

    EXPORTED void pluginExit( UARTPlugin *ptrPlugin )
    {
        if( nullptr != ptrPlugin )
        {
            delete ptrPlugin;
        }
    }
}


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/**
  * \brief Function where to execute initialization of sub-modules
*/

bool UARTPlugin::doInit(void *pvUserData)
{
    swlFHInit();

    m_bIsInitialized = true;

    return m_bIsInitialized;

}


/**
  * \brief Function where to execute de-initialization of sub-modules
*/

void UARTPlugin::doCleanup(void)
{
    swlFHDeInit();

    m_bIsInitialized = false;
    m_bIsEnabled     = false;

}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////


/**
  * \brief INFO command implementation; shows details about plugin and
  *        describe the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails
  *
  * \note Usage example: <br>
  *       UART.INFO
  *
  * \param[in] pstrArgs NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/

bool UARTPlugin::m_UART_INFO ( const char *pstrArgs ) const
{
    bool bRetVal = false;

    do {

        // expected no arguments
        if( nullptr != pstrArgs )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if( false == m_bIsEnabled )
        {
            bRetVal = true;
            break;
        }

        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Version:"); DLT_STRING(m_strPluginVersion.c_str()));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Build:"); DLT_STRING(__DATE__); DLT_STRING(__TIME__));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Description: communicate with other apps/devices via UART"));
        DLT_SHOW_CAPTION(nullptr,nullptr, 3);
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("SET_UART_PORT : overwrite the default UART port"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Args : [port]"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Usage: UART.SET_UART_PORT COM5"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("       UART.SET_UART_PORT $NEW_PORT"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Note : If no port is given then the default port remains unchanged"));
        DLT_SHOW_CAPTION(nullptr,nullptr, 3);
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("READ : read and print data from the UART port until the read timeout occurs"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Args : [timeout]"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Usage: UART.READ"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("       UART.READ 5000"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Note : If timeout is not specified then the default UART read timeout is used"));
        DLT_SHOW_CAPTION(nullptr,nullptr, 3);
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("WRITE : send an item and wait for an answer "));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Args : item [| answer]"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("       item: string, hexstream, filename"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("       answer: string, hexstream, regex"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Usage: UART.WRITE gpt list --known"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("       UART.WRITE gpt list --known | return value 0"));
        DLT_SHOW_CAPTION(nullptr,nullptr, 3);
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("SCRIPT : send commands from a file"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Args : script"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Usage: UART.SCRIPT script.txt"));
        DLT_SHOW_CAPTION(nullptr,nullptr, 3);
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("WAIT : wait for an item"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Args : item [timeout]"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Usage: UART.WAIT\"return value 0\""));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("       UART.WAIT \"return value 0\"  2000"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("Note : If timeout is not specified then the default UART read timeout is used"));
        DLT_LOG(UartPluginCtx, DLT_LOG_FIXED, DLT_HDR; DLT_STRING("       The string has to be placed in quotes "));
        DLT_SHOW_CAPTION(nullptr,nullptr, 3);

        bRetVal = true;

    } while(false);

    return bRetVal;

}

/**
  * \brief READ command implementation;
  *
  * \note Usage example: <br>
  *       UART.READ
  *       UART.READ 100
  *
  * \param[in] pstrArgs - optional timeout
  *
  * \return true on success, false otherwise
*/

bool UARTPlugin::m_UART_READ ( const char *pstrArgs ) const
{
    bool bRetVal = false;

    do {

        uint32_t uiReadTimeout = m_u32ReadTimeout;

        if( nullptr != pstrArgs )
        {
            // fail if more than one space separated arguments is provided ...
            if( true == string_contains_char(std::string(pstrArgs), CHAR_SEPARATOR_SPACE) )
            {
                DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Missing: timeout"));
                break;
            }

            if( false == string_to_ui32(pstrArgs ,&uiReadTimeout) )
            {
                break;
            }
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if( false == m_bIsEnabled )
        {
            bRetVal = true;
            break;
        }

        bRetVal = uart_hdl_read_lines( m_strUartPort.c_str(), m_u32UartBaudrate, uiReadTimeout, m_u32UartReadBufferSize );

    } while(false);

    return bRetVal;

}


/**
  * \brief WRITE command implementation;
  *
  * \note Usage example: <br>
  *       UART.WRITE item [| item]
  *
  * \param[in] pstrArgs NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/

bool UARTPlugin::m_UART_WRITE ( const char *pstrArgs ) const
{
    bool bRetVal = false;

    do {

        if( nullptr == pstrArgs )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Missing item [|answer]"));
            break;
        }

        if( true == m_bIsEnabled )
        {
            if( false == uart_hdl_open_port( m_strUartPort.c_str(), m_u32UartBaudrate, &m_i32UartHandle ) )
            {
                break;
            }
        }

        if( false == m_UART_CommandProcessor( std::string(pstrArgs), 0, 0) )
        {
            break;
        }

        bRetVal = true;

    } while(false);

    if( true == m_bIsEnabled )
    {
        uart_hdl_close_port(m_i32UartHandle);
        m_i32UartHandle = -1;
    }

    return bRetVal;

}


/**
  * \brief WAIT command implementation;
  *
  * \note Usage example: <br>
  *       UART.WAIT some_string
  *
  * \param[in] string to wait for
  *
  * \return true on success, false otherwise
*/

bool UARTPlugin::m_UART_WAIT ( const char *pstrArgs ) const
{
    bool bRetVal = false;

    do {

        if( nullptr == pstrArgs )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Missing arg(s): item [timeout]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        string_tokenize_space<const char*>(pstrArgs, vstrArgs);
        size_t szNrArgs = vstrArgs.size();
        uint32_t uiReadTimeout = m_u32ReadTimeout;

        // Expected: item [| delay]
        if( (szNrArgs < 1) || (szNrArgs > 2) )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Expected arg(s): item [timeout]"));
            break;
        }

        // timeout provided
        if( 2 == szNrArgs )
        {
            if( false == string_to_ui32(vstrArgs[1].c_str() ,&uiReadTimeout) )
            {
                break;
            }
        }

        // get the item type to wait for
        std::string strItem;
        std::vector<uint8_t> vDataItem;
        ItemType_e eTypeWaited = m_UART_GetItemType(vstrArgs[0], strItem, vDataItem);

        // invalid item provided, abort execution during the validation phase
        if( ItemType_e::ITEM_TYPE_LAST == eTypeWaited )
        {
            break;
        }

        if( true == m_bIsEnabled )
        {
            if( false == uart_hdl_open_port( m_strUartPort.c_str(), m_u32UartBaudrate, &m_i32UartHandle ) )
            {
                break;
            }
        }

        bRetVal = m_UART_WaitItem( strItem, vDataItem, eTypeWaited, uiReadTimeout );

    } while(false);

    if( true == m_bIsEnabled )
    {
        uart_hdl_close_port(m_i32UartHandle);
        m_i32UartHandle = -1;
    }

    return bRetVal;

}


/**
  * \brief SCRIPT command implementation;
  *
  * \note Usage example: <br>
  *       UART.SCRIPT scriptname [|delay]
  *
  * \param[in] filename<string>
  *
  * \return true on success, false otherwise
*/

bool UARTPlugin::m_UART_SCRIPT ( const char *pstrArgs ) const
{
    bool bRetVal = false;

    do {

       // expected to have as parameter the name of the script
        if( nullptr == pstrArgs )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Missing arg(s): scriptname scriptargs [|delay]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        string_tokenize_space<const char*>(pstrArgs, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        if( (szNrArgs < 2) || (szNrArgs > 3) )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Expected: scriptname scriptargs [|delay] "));
            break;
        }

        uint32_t uiDelay = 0;
        // delay was provided as argument
        if( 3 == szNrArgs )
        {
            if( false == string_to_ui32(vstrArgs[1].c_str() ,&uiDelay) )
            {
                break;
            }
        }

        char *pstrPathFileName = nullptr;
        const char *pstrArtefactsPath = (true == m_strArtefactsPath.empty()) ? nullptr : m_strArtefactsPath.c_str();

        // check the file validity
        if( false == swlFHCheckFileAvailability(pstrArtefactsPath, vstrArgs[0].c_str(), &pstrPathFileName, nullptr) )
        {
            break;
        }

        // create an instance of the script parser
        if( nullptr == (m_pScriptParser = new ScriptParser(std::string(pstrPathFileName), vstrArgs[1])) )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Failed to create instance of script parser"));
            break;
        }

        // register the command of plugin specific line processor
        m_pScriptParser->RegisterLineProcessor([this](const std::string& strLine, const uint32_t uiLineIdx, const uint8_t u8FieldWidth) -> bool
        {
            return m_UART_CommandProcessor( strLine, uiLineIdx, u8FieldWidth);
        });

        if( true == m_bIsEnabled )
        {
            if( false == uart_hdl_open_port( m_strUartPort.c_str(), m_u32UartBaudrate, &m_i32UartHandle ) )
            {
                break;
            }
        }

        uint32_t uiDelayRef = (true == m_bIsEnabled) ? uiDelay : 0;
        DLT_LOG(UartPluginCtx, DLT_LOG_INFO, DLT_HDR; DLT_STRING("---"); DLT_STRING((false == m_bIsEnabled) ? "Validate" : "Execute"); DLT_STRING("script:"); DLT_STRING(pstrPathFileName); DLT_STRING("| delay:"); DLT_UINT32(uiDelayRef); DLT_STRING("---"));

        // parse the script and validate the commands
        if( false == m_pScriptParser->Run(uiDelayRef) )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("---"); DLT_STRING("Script"); DLT_STRING((false == m_bIsEnabled) ? "validation" : "execution"); DLT_STRING("failed");DLT_STRING("---"));
            break;
        }

        DLT_LOG(UartPluginCtx, DLT_LOG_INFO, DLT_HDR; DLT_STRING("---"); DLT_STRING("End of script"); DLT_STRING((false == m_bIsEnabled) ? "validation" : "execution"); DLT_STRING("---"));

        bRetVal = true;

    } while(false);

    if( true == m_bIsEnabled )
    {
        uart_hdl_close_port(m_i32UartHandle);
        m_i32UartHandle = -1;
    }

    return bRetVal;

}


/**
  * \brief SET_UART_PORT command implementation; overwrite the current UART port (m_strUartPort)
  *
  * \note If an empty string is provided then the command doesn't change anything
  *
  * \note Is intended to change the port when a virtual UART over USB is used
  *
  * \note Usage example: <br>
  *       UART.SET_UART_PORT COM5
  *       UART.SET_UART_PORT /dev/ttyUSB0
  *       UART.SET_UART_PORT
  *
  * \param[in] new port as string or am empty string
  *
  * \return true if reading succeeded, false otherwise
*/

bool UARTPlugin::m_UART_SET_UART_PORT ( const char *pstrArgs ) const
{
    return uart_generic_change_port<UARTPlugin>(this, pstrArgs);

}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/**
 * \brief The class member method used to process of a script line
 * \param[in] strLine script line to be processed
 * \param[in] uiLineIdx the index of the line to be processed
 * \param[in] field size for showing the line number
 * \return true on success, false otherwise
 */

bool UARTPlugin::m_UART_CommandProcessor ( const std::string& strLine, const uint32_t uiLineIdx, const uint8_t u8FieldWidth ) const
{
    bool bRetVal = false;

    do {

        if( 0 != uiLineIdx )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_DEBUG, DLT_HDR; DLT_STRING("@ line"); DLT_UINT32_ALIGNED(uiLineIdx, u8FieldWidth); DLT_STRING(":"); DLT_STRING(strLine.c_str()));
        }

        bool bOnlyWait  = string_starts_with(strLine, STRING_SEPARATOR_VERTICAL_BAR);
        bool bWrongTail = string_ends_with(strLine, STRING_SEPARATOR_VERTICAL_BAR);

        // split line at STRING_SEPARATOR_VERTICAL_BAR to separate the message from the expected answer (if any)
        std::vector<std::string> vstrArgs;
        string_split(strLine, STRING_SEPARATOR_VERTICAL_BAR, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        // Expected: item, item|answer, |answer
        if( ((szNrArgs < 1) || (szNrArgs > 2)) || ((1 != szNrArgs) && (true == bOnlyWait)) || (true == bWrongTail) )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Unsupported, try: item, item|answer, |answer "));
            break;
        }

        std::string strItem;
        std::vector<uint8_t> vDataItem;
        ItemType_e eTypeItem = m_UART_GetItemType(vstrArgs[0], strItem, vDataItem);

        if( ItemType_e::ITEM_TYPE_LAST == eTypeItem )
        {
            break;
        }

        // either write or wait the item depending if the expression starts with STRING_SEPARATOR_VERTICAL_BAR
        if( 1 == szNrArgs )
        {
            bRetVal = bOnlyWait ? m_UART_WaitItem( strItem, vDataItem, eTypeItem, m_u32ReadTimeout ) : m_UART_WriteItem( strItem, vDataItem, eTypeItem );
            break;
        }

        // send the message and wait for the answer specified after the STRING_SEPARATOR_VERTICAL_BAR separator
        if( 2 == szNrArgs )
        {
            std::string strAnswer;
            std::vector<uint8_t> vDataAnswer;
            ItemType_e eTypeAnswer = m_UART_GetItemType(vstrArgs[1], strAnswer, vDataAnswer);

            if( ItemType_e::ITEM_TYPE_LAST == eTypeAnswer )
            {
                break;
            }

            bRetVal = (true == m_UART_WriteItem(strItem, vDataItem, eTypeItem)) ? m_UART_WaitItem(strAnswer, vDataAnswer, eTypeAnswer, m_u32ReadTimeout) : false;
            break;
        }

    } while(false);

    if( false == bRetVal )
    {
        if( 0 != uiLineIdx )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Failed at script line:"); DLT_UINT32(uiLineIdx));
        }
    }

    return bRetVal;

}


/**
 * \brief The class member method used to validate an expression
 * \param[in] strInput the input string
 * \param[out] strOutput the input string after being cleaned up by the header/tail used to identify it's type
 * \return true on success, false otherwise
 */

UARTPlugin::ItemType_e UARTPlugin::m_UART_GetItemType ( const std::string& strInput, std::string& strOutput, std::vector<uint8_t>& vData ) const
{
    ItemType_e eItemType = ItemType_e::ITEM_TYPE_LAST;

    do {

        // check for hexstream
        if( true == string_undecorate( strInput, std::string("H("), std::string(")"), strOutput) )
        {
            // remove empty spaces from the string
            string_remove_spaces(strOutput);

            if( true == strOutput.empty() )
            {
                DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Empty hexstream: not supported"));
                break;
            }

            DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Hexstream |"); DLT_STRING(strOutput.c_str()); DLT_STRING("|"));

            // check if it's a valid hex stream
            if( false == string_unhexlify<uint8_t>(strOutput, vData) )
            {
                DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Failed to convert hexstream to buffer"));
                break;
            }

            DLT_LOG_DUMP("Hexstream data", (const char *)vData.data(), (int)vData.size(), false, DLT_LOG_VERBOSE);
            eItemType = ItemType_e::ITEM_TYPE_HEXSTREAM;
            break;
        }

        // check for regex
        if( true == string_undecorate( strInput, std::string("R("), std::string(")"), strOutput) )
        {
            // trim spaces around the regex
            string_trim_inplace(strOutput);

            if( true == strOutput.empty() )
            {
                DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Empty regex: not supported"));
                break;
            }

            DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Regex |"); DLT_STRING(strOutput.c_str()); DLT_STRING("|"));
            eItemType = ItemType_e::ITEM_TYPE_REGEX;
            break;
        }

        // check for filename
        if( true == string_undecorate( strInput, std::string("F("), std::string(")"), strOutput) )
        {
            // trim spaces around the filename
            string_trim_inplace(strOutput);

            if( true == strOutput.empty() )
            {
                DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Empty filename not supported"));
                break;
            }

            DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("File |"); DLT_STRING(strOutput.c_str()); DLT_STRING("|"));
            eItemType = ItemType_e::ITEM_TYPE_FILENAME;
            break;
        }

        // check for simple decorated string
        if( true == string_undecorate(strInput, strOutput))
        {
            // empty string inside
            if( true == strOutput.empty() )
            {
                DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Decorated string empty"));
                eItemType = ItemType_e::ITEM_TYPE_EMPTY_STRING;
                break;
            }

            // non-empty string inside decorations
            DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Decorated string |"); DLT_STRING(strOutput.c_str()); DLT_STRING("|"));
            eItemType = ItemType_e::ITEM_TYPE_STRING;
            break;
        }

        // check for simple undecorated string
        if( true == strInput.empty() )
        {

            DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("String empty"));
            eItemType = ItemType_e::ITEM_TYPE_EMPTY_STRING;
            break;
        }

        strOutput = strInput;
        DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("String |"); DLT_STRING(strOutput.c_str()); DLT_STRING("|"));
        eItemType = ItemType_e::ITEM_TYPE_STRING;

    } while(false);

    return eItemType;

}


/**
  * \brief Send a message of type string or hexbuffer (regex is not a valid type for a message to be sent)
  * \param[in] strItem string to be sent
  * \param[in] vDataItem buffer containing the unhexlified strItem
  * \param[in] eTypeSend type of the message as enumeration UARTPlugin::ItemType_e
  * \return true on success, false otherwise
*/

bool UARTPlugin::m_UART_WriteItem( const std::string& strItem, std::vector<uint8_t>& vDataItem, UARTPlugin::ItemType_e eTypeSend ) const
{
    bool bRetVal = false;

    switch( eTypeSend )
    {
        case ItemType_e::ITEM_TYPE_HEXSTREAM:
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Writing: hexstream | timeout:"); DLT_UINT32(m_u32WriteTimeout));
            bRetVal = (false == m_bIsEnabled) ? true : uart_hdl_send_buf( m_i32UartHandle, m_u32WriteTimeout, reinterpret_cast<const char*>(vDataItem.data()), (uint32_t)vDataItem.size());
            break;
        }

        case ItemType_e::ITEM_TYPE_STRING:
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Writing: string |"); DLT_STRING(strItem.c_str()); DLT_STRING("| timeout:"); DLT_UINT32(m_u32WriteTimeout));
            bRetVal = (false == m_bIsEnabled) ? true : uart_hdl_send_msg( m_i32UartHandle, m_u32WriteTimeout, strItem.c_str(), true);
            break;
        }

        case ItemType_e::ITEM_TYPE_FILENAME:
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Writing: file |"); DLT_STRING(strItem.c_str()); DLT_STRING("|"));
            bRetVal = m_UART_WriteFile( strItem );
            break;
        }

        default:
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Writing: type unsupported"));
            break;
        }
    }

    return bRetVal;

}


/**
  * \brief Wait for a message of type string, hexbuffer or regex
  * \param[in] strItem string to be waited for
  * \param[in] vDataItem buffer containing the unhexlified answer
  * \param[in] eTypeWaited type to be waited for as enumeration UARTPlugin::ItemType_e
  * \param[in] uiReadTimeout timeout to wait for during the reading
  * \return true on success, false otherwise
*/

bool UARTPlugin::m_UART_WaitItem( const std::string& strItem, std::vector<uint8_t>& vDataItem, UARTPlugin::ItemType_e eTypeWaited, const uint32_t uiReadTimeout ) const
{
    bool bRetVal = false;

    switch(eTypeWaited)
    {
        case ItemType_e::ITEM_TYPE_HEXSTREAM:
        {
            if( false == m_bIsEnabled )
            {
                bRetVal = true;
            }
            else
            {
                DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Waiting: hexstream | timeout:"); DLT_UINT32(uiReadTimeout));

                // read the lines until the expected item occurence
                if( true == (bRetVal = uart_hdl_wait_token_buffer( m_i32UartHandle, uiReadTimeout, reinterpret_cast<const char*>(vDataItem.data()), (uint32_t)vDataItem.size())) )
                {
                    // read the lines after the item occurence (if any)
                    bRetVal = uart_hdl_read_lines( m_i32UartHandle, m_u32UartReadBufferTout, m_u32UartReadBufferSize );

                }

            }

            break;
        }

        case ItemType_e::ITEM_TYPE_STRING:
        {
            if( false == m_bIsEnabled )
            {
                bRetVal = true;
            }
            else
            {
                DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Waiting: string |"); DLT_STRING(strItem.c_str()); DLT_STRING("| timeout:"); DLT_UINT32(uiReadTimeout));

                // read the lines until the expected item occurence
                if( true == (bRetVal = uart_hdl_wait_line( m_i32UartHandle, uiReadTimeout, strItem.c_str())) )
                {
                    // read the lines after the item occurence (if any)
                    bRetVal = uart_hdl_read_lines( m_i32UartHandle, m_u32UartReadBufferTout, m_u32UartReadBufferSize );
                }
            }

            break;
        }

        case ItemType_e::ITEM_TYPE_REGEX:
        {
            if( false == m_bIsEnabled )
            {
                bRetVal = true;
            }
            else
            {
                DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Waiting: string |"); DLT_STRING(strItem.c_str()); DLT_STRING("| timeout:"); DLT_UINT32(uiReadTimeout));

                std::vector<std::string> vstrGroups;
                if( true == (bRetVal = uart_hdl_wait_line( m_i32UartHandle, uiReadTimeout, strItem.c_str(), vstrGroups)) )
                {
                    if( false == vstrGroups.empty() )
                    {
                        DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Got groups:"); DLT_UINT32((uint32_t)vstrGroups.size()));
                        string_merge_vector_content(vstrGroups, m_strResultData, STRING_SEPARATOR_SLASH);
                        string_trim_inplace(m_strResultData, STRING_SEPARATOR_SLASH);
                    }

                    // read the lines after the item occurence (if any)
                    bRetVal = uart_hdl_read_lines( m_i32UartHandle, m_u32UartReadBufferTout, m_u32UartReadBufferSize );
                }
            }

            break;
        }

        // used to empty the read buffer in case when nothing special is to be waited for and has to be triggered with empty decorated string after vertica bar, like this: | ""
        case ItemType_e::ITEM_TYPE_EMPTY_STRING:
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Waiting: everything"); DLT_STRING("| timeout:"); DLT_UINT32(m_u32UartReadBufferTout));

            if( false == m_bIsEnabled )
            {
                bRetVal = true;
            }
            else
            {
                bRetVal = uart_hdl_read_lines( m_i32UartHandle, m_u32UartReadBufferTout, m_u32UartReadBufferSize );
            }

            break;
        }

        default:
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Waiting: unsupported type!"));
            break;
        }
    }

    return bRetVal;

}


/**
  * \brief Validate file
  * \param[in] strItem string containing filename[:chunksize]
  * \param[out] ppstrFilePathName pointer where the pathfilename is stored
  * \param[out] pi64FileSize pointer to filesize
  * \param[out] puiChunksize pointer to chunksize
  * \return true on success, false otherwise
*/

bool UARTPlugin::m_UART_ValidateFile( const std::string& strItem, char **ppstrFilePathName, int64_t *pi64FileSize, uint32_t *puiChunkSize ) const
{
    bool bRetVal = false;

    do {

        // split filename : chunksize
        std::vector<std::string> vstrArgs;
        string_tokenize(strItem, STRING_SEPARATOR_COLON, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        // expected filename [:chunksize]
        if( (szNrArgs < 1) || (szNrArgs > 2) )
        {
            DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Expected args: filename [:chunksize]"));
            break;
        }

        uint32_t uiChunkSize = 0;

        // the format is filename:chunksize
        if( 2 == szNrArgs )
        {
            if( false == string_to_ui32(vstrArgs[1].c_str(), puiChunkSize) )
            {
                DLT_LOG(UartPluginCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Wrong chunksize value:"); DLT_STRING(vstrArgs[1].c_str()));
                break;
            }
        }

        const char *pstrArtefactsPath = (true == m_strArtefactsPath.empty()) ? nullptr : m_strArtefactsPath.c_str();
        const char *pstrFileName = vstrArgs[0].c_str();

        if( false == swlFHCheckFileAvailability( pstrArtefactsPath, pstrFileName, ppstrFilePathName, pi64FileSize) )
        {
            break;
        }

        bRetVal = true;

    } while(false);

    return bRetVal;

}


/**
  * \brief Upload a file
  * \param[in] strItem file description, filename[:chunksize]
  * \return true on success, false otherwise
*/

bool UARTPlugin::m_UART_WriteFile( const std::string& strItem ) const
{
    bool bRetVal = false;

    do {

        char *pstrFilePathName = nullptr;
        int64_t  i64FileSize = 0;
        uint32_t uiChunkSize = 0;

        // check the item's format and the file's existence
        if( false == m_UART_ValidateFile(strItem, &pstrFilePathName, &i64FileSize, &uiChunkSize) )
        {
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if( false == m_bIsEnabled )
        {
            bRetVal = true;
            break;
        }

        uint32_t uiCrc32 = 0;

        bRetVal = (0 != uiChunkSize) ? upload_hdl_uart_upload_file(pstrFilePathName, i64FileSize, uiChunkSize, m_i32UartHandle, m_u32WriteTimeout, &uiCrc32)
                                     : upload_hdl_uart_upload_file(pstrFilePathName, i64FileSize, m_i32UartHandle, m_u32WriteTimeout, &uiCrc32);

    } while(false);

    return bRetVal;

}


bool UARTPlugin::m_LocalSetParams( const PluginDataSet *psSetParams )
{
    bool bRetVal = false

    if (!psSetParams->mapSettings.empty()) {
        do {
            if (psSetParams->mapSettings.count(ARTEFEACTS_PATH) > 0) {
                m_strArtefactsPath = psSetParams->mapSettings.at(ARTEFEACTS_PATH);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ArtefactsPath :"); LOG_STRING(m_strArtefactsPath));
            }

            if (psSetParams->mapSettings.count(COM_PORT) > 0) {
                m_strUartPort = psSetParams->mapSettings.at(COM_PORT);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Port :"); LOG_STRING(m_strUartPort));
            }

            if (psSetParams->mapSettings.count(BAUDRATE) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(BAUDRATE), &m_u32UartBaudrate)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Baudrate :"); LOG_UINT32(m_u32UartBaudrate));
            }

            if (psSetParams->mapSettings.count(READ_TIMEOUT) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(READ_TIMEOUT), &m_u32ReadTimeout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadTimeout :"); LOG_UINT32(m_u32ReadTimeout));
            }

            if (psSetParams->mapSettings.count(WRITE_TIMEOUT) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(WRITE_TIMEOUT), &m_u32WriteTimeout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("WriteTimeout :"); LOG_UINT32(m_u32WriteTimeout));
            }

            if (psSetParams->mapSettings.count(READ_BUF_SIZE) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(READ_BUF_SIZE), &m_u32UartReadBufferSize)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadBufSize :"); LOG_UINT32(m_u32UartReadBufferSize));
            }

            if (psSetParams->mapSettings.count(READ_BUF_TIMEOUT) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(READ_BUF_TIMEOUT), &m_u32UartReadBufferTout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadBufTout :"); LOG_UINT32(m_u32UartReadBufferTout));
            }

            bRetVal = true;

        } while(false);
    }

    return bRetVal;

}
