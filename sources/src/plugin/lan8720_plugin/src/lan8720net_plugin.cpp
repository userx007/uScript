#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "lan8720net_setup.hpp"
#include "lan8720net_plugin.hpp"
#include "uLan8720Net.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"

#include <memory>

#ifdef LT_HDR
    #undef LT_HDR
#endif
#define LT_HDR "LAN8720NET PLUGIN |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define ARTEFACTS_PATH              "ARTEFACTS_PATH"
#define SERVER_IP                   "SERVER_IP"
#define SERVER_PORT                 "SERVER_PORT"
#define READ_TIMEOUT                "READ_TIMEOUT"
#define WRITE_TIMEOUT               "WRITE_TIMEOUT"
#define READ_BUFFER_SIZE            "READ_BUFFER_SIZE"

extern "C"
{
    EXPORTED Lan8720NetPlugin* pluginEntry()
    {
        return new Lan8720NetPlugin();
    }

    EXPORTED void pluginExit( Lan8720NetPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

bool Lan8720NetPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}

void Lan8720NetPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
    m_strResultData.clear();
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));
}

bool Lan8720NetPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {
            auto it = psSetParams->mapSettings.find(ARTEFACTS_PATH);
            if (it != psSetParams->mapSettings.end()) {
                m_strArtefactsPath = it->second;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ArtefactsPath :"); LOG_STRING(m_strArtefactsPath));
            }

            it = psSetParams->mapSettings.find(SERVER_IP);
            if (it != psSetParams->mapSettings.end()) {
                setServerIp(it->second);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Server IP :"); LOG_STRING(m_strServerIp));
            }

            it = psSetParams->mapSettings.find(SERVER_PORT);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setServerPort(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Server Port :"); LOG_UINT32(m_u16ServerPort));
            }

            it = psSetParams->mapSettings.find(READ_TIMEOUT);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setReadTimeout(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Read Timeout :"); LOG_UINT32(m_u32ReadTimeout));
            }

            it = psSetParams->mapSettings.find(WRITE_TIMEOUT);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setWriteTimeout(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Write Timeout :"); LOG_UINT32(m_u32WriteTimeout));
            }

            it = psSetParams->mapSettings.find(READ_BUFFER_SIZE);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setReadBufferSize(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Read Buffer Size :"); LOG_UINT32(m_u32ReadBufferSize));
            }

            bRetVal = true;

        } while(false);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        bRetVal = true;
    }

    return bRetVal;
}

bool Lan8720NetPlugin::setServerPort (const std::string& strServerPort) const
{
    static constexpr uint32_t TCP_PORT_MAX = 65535U;
    uint32_t u32Port = 0U;
    if (false == numeric::str2uint32(strServerPort, u32Port)) {
        return false;
    }
    if (u32Port == 0U || u32Port > TCP_PORT_MAX) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Port out of range [1-65535]:"); LOG_UINT32(u32Port));
        return false;
    }
    m_u16ServerPort = static_cast<uint16_t>(u32Port);
    return true;
}

bool Lan8720NetPlugin::setReadTimeout (const std::string& strReadTimeout) const
{
    return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
}

bool Lan8720NetPlugin::setWriteTimeout (const std::string& strWriteTimeout) const
{
    return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
}

bool Lan8720NetPlugin::setReadBufferSize (const std::string& strReadBufferSize) const
{
    static constexpr uint32_t MAX_BUF = 1460U;
    uint32_t u32Size = 0U;
    if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
        return false;
    }
    if (u32Size == 0U || u32Size > MAX_BUF) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ReadBufSize out of range [1-"); LOG_UINT32(MAX_BUF); LOG_STRING("]:"); LOG_UINT32(u32Size));
        return false;
    }
    m_u32ReadBufferSize = u32Size;
    return true;
}

std::shared_ptr<Lan8720Net> Lan8720NetPlugin::m_OpenDriver (void) const
{
    if (m_strServerIp.empty() || m_u16ServerPort == 0U) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Server IP/Port not configured"));
        return nullptr;
    }

    auto shpDriver = std::make_shared<Lan8720Net>();

    if (Lan8720Net::Status::SUCCESS != shpDriver->open(m_strServerIp, m_u16ServerPort)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Connect failed:"); LOG_STRING(m_strServerIp.c_str());
                  LOG_STRING(":"); LOG_UINT32(m_u16ServerPort));
        return nullptr;
    }

    return shpDriver;
}

bool Lan8720NetPlugin::m_LAN8720NET_INFO(const std::string& args, std::stop_token st) const
{
    (void)st;

    // expected no arguments
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled)
    {
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(LAN8720NET_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(LAN8720NET_PLUGIN_VERSION));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via TCP/IP over a LAN8720 Ethernet PHY (client socket)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the server IP, port and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i:ip] [p:port] [r:read_tout] [w:write_tout] [s:recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : LAN8720NET.CONFIG i:192.168.1.10 p:5000 r:2000 w:2000 s:512"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         LAN8720NET.CONFIG i:localhost p:8080"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : any subset of keys may be given; omitted keys retain their current values"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : LAN8720NET.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : LAN8720NET.CMD > Hello | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         LAN8720NET.CMD < \"Please send!\" | Sending..."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a fresh connection to ip:port is opened for CMD and closed once it completes"));
    LOG_SEP();

    return true;
}

bool Lan8720NetPlugin::m_LAN8720NET_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();
    return generic_lan8720net_set_params(this, args);
}

bool Lan8720NetPlugin::m_LAN8720NET_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    bool bRetVal = false;
    resetData();

    do {
        if (args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing command"));
            break;
        }

        if (!m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        try {
            auto shpDriver = m_OpenDriver();
            if (shpDriver) {
                CommScriptCommandValidator validator;
                CommCommand command;

                if (validator.validateCommand(0, args, command)) {
                    CommScriptCommandInterpreter<Lan8720Net> interpreter(
                        shpDriver,
                        m_u32ReadBufferSize,
                        m_u32ReadTimeout
                    );
                    bRetVal = interpreter.interpretCommand(command, m_bIsEnabled);
                }
            }
        } catch (const std::bad_alloc& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }

    } while (false);

    return bRetVal;
}

bool Lan8720NetPlugin::m_LAN8720NET_SCRIPT(const std::string& args, std::stop_token st) const
{
    bool bRetVal = false;
    resetData();

    do {
        if (args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing arg(s): scriptpathname [|delay]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        if ((szNrArgs < 1) || (szNrArgs > 2)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: scriptpathname [|delay] "));
            break;
        }

        size_t szDelay = 0;
        if (2 == szNrArgs) {
            if (!numeric::str2sizet(vstrArgs[1], szDelay)) {
                break;
            }
        }

        std::string strScriptPathName;
        ufile::buildFilePath(m_strArtefactsPath, vstrArgs[0], strScriptPathName);

        if (!ufile::fileExistsAndNotEmpty(strScriptPathName)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found or empty:"); LOG_STRING(strScriptPathName));
            break;
        }

        try {
            auto shpDriver = m_OpenDriver();
            if (shpDriver) {
                CommScriptClient<Lan8720Net> client(
                    strScriptPathName,
                    shpDriver,
                    m_u32ReadBufferSize,
                    m_u32ReadTimeout,
                    szDelay
                );
                bRetVal = client.execute(m_bIsEnabled);
            }
        } catch (const std::bad_alloc& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }

    } while(false);

    return bRetVal;
}
