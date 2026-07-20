#ifndef UCOMMANDEXEC_HPP
#define UCOMMANDEXEC_HPP

#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

/**
  * \brief Shared CMD/SCRIPT command-handler bodies used by the comm-driver
  *        plugins (UART, KI2C, KSPI, CH341, KVCAN, DSPKI2C, DSPKSPI, PCAN,
  *        SLCAN, TCPIP, UDP, LAN8720NET, W5500NET, ENC28J60NET, RawEth,
  *        MQTT, ...).
  *
  *        Every one of these plugins' CMD/SCRIPT handlers follows the same
  *        shape:
  *          CMD:    reject empty args -> early-return true if the plugin
  *                  isn't enabled (argument validation without execution)
  *                  -> open the driver -> validate + run one command via
  *                  CommScriptCommandInterpreter.
  *          SCRIPT: tokenize "scriptpathname [|delay]" -> resolve and
  *                  sanity-check the script file -> open the driver -> run
  *                  it via CommScriptClient.
  *
  *        The only thing that actually differs between plugins is *how* a
  *        driver instance gets opened/configured - constructor arguments,
  *        is_open() vs. an open()+Status check, extra one-time setup like
  *        KVCAN's set_tx_id()/set_filters(). That single difference is
  *        captured by a caller-supplied "open" callable, the same
  *        dependency-injection approach uKmpMatch.hpp uses for the
  *        driver-level KMP token matching (there: inject how to read a
  *        byte/chunk; here: inject how to open a driver).
  *
  *        This header used to be copy-pasted, boilerplate and all, into
  *        every plugin's *_plugin.cpp. Factoring it out means a fix (e.g.
  *        the SCRIPT arg-count bounds check below) only needs to be made
  *        once instead of sixteen times.
*/
namespace ucmdexec
{

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Shared body for a plugin's *_CMD command.
  *
  *        DriverT is deduced from openFn's return type (std::shared_ptr<DriverT>),
  *        so call sites never need to spell out an explicit template argument.
  *
  * \param[in] args              the command's argument string (as received by the plugin's dispatch)
  * \param[in] bIsEnabled        the plugin's current enabled state
  * \param[in] openFn            callable: () -> std::shared_ptr<DriverT>; returns nullptr (and should
  *                                log why) on failure to open/configure the driver
  * \param[in] szReadBufferSize  read-buffer size forwarded to CommScriptCommandInterpreter<DriverT>
  * \param[in] u32ReadTimeout    default read timeout forwarded to CommScriptCommandInterpreter<DriverT>
  * \param[in] pszLogHdr         log header literal used to prefix error messages, e.g. "UART        |"
  *
  * \return true on success (including the "plugin not enabled" early-out, which validates
  *          arguments without executing), false on a validation or execution failure
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename OpenFn>
bool generic_cmd(const std::string& args,
                  bool bIsEnabled,
                  OpenFn&& openFn,
                  size_t szReadBufferSize,
                  uint32_t u32ReadTimeout,
                  const char* pszLogHdr)
{
    using DriverT = typename std::invoke_result_t<OpenFn>::element_type;

    bool bRetVal = false;

    do
    {
        if (args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("Missing command"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (!bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        try
        {
            auto shpDriver = openFn();

            if (shpDriver)
            {
                CommScriptCommandValidator validator;
                CommCommand command;

                if (validator.validateCommand(0, args, command))
                {
                    CommScriptCommandInterpreter<DriverT> interpreter(shpDriver, szReadBufferSize, u32ReadTimeout);
                    bRetVal = interpreter.interpretCommand(command, bIsEnabled);
                }
            }
        }
        catch (const std::bad_alloc& e)
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        }
        catch (const std::exception& e)
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }

    } while (false);

    return bRetVal;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Shared body for a plugin's *_SCRIPT command.
  *
  *        DriverT is deduced the same way as in generic_cmd().
  *
  *        Note: the arg-count check below is `(szNrArgs < 1) || (szNrArgs > 2)`.
  *        A couple of the original per-plugin copies had weakened this to
  *        just `szNrArgs > 2` (reasoning that args being non-empty already
  *        guarantees at least one token) - but a whitespace-only args string
  *        tokenizes to zero tokens despite not being std::string::empty(),
  *        which would have reached vstrArgs[0] with szNrArgs == 0. Keeping
  *        the full bounds check here fixes that latent out-of-bounds access
  *        for every plugin at once.
  *
  * \param[in] args              the command's argument string: "scriptpathname [|delay]"
  * \param[in] bIsEnabled        the plugin's current enabled state (forwarded to CommScriptClient::execute)
  * \param[in] openFn            callable: () -> std::shared_ptr<DriverT>, see generic_cmd()
  * \param[in] strArtefactsPath  base directory scriptpathname is resolved against
  * \param[in] szReadBufferSize  read-buffer size forwarded to CommScriptClient<DriverT>
  * \param[in] u32ReadTimeout    default read timeout forwarded to CommScriptClient<DriverT>
  * \param[in] pszLogHdr         log header literal used to prefix error messages
  *
  * \return true on success, false on a validation, file, or execution failure
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename OpenFn>
bool generic_script(const std::string& args,
                     bool bIsEnabled,
                     OpenFn&& openFn,
                     const std::string& strArtefactsPath,
                     size_t szReadBufferSize,
                     uint32_t u32ReadTimeout,
                     const char* pszLogHdr)
{
    using DriverT = typename std::invoke_result_t<OpenFn>::element_type;

    bool bRetVal = false;

    do
    {
        // expected to have as parameter the name of the script
        if (args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("Missing arg(s): scriptpathname [|delay]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
        const size_t szNrArgs = vstrArgs.size();

        if ((szNrArgs < 1) || (szNrArgs > 2))
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("Expected: scriptpathname [|delay]"));
            break;
        }

        size_t szDelay = 0;
        if (szNrArgs == 2)
        {
            if (!numeric::str2sizet(vstrArgs[1], szDelay))
            {
                break;
            }
        }

        std::string strScriptPathName;
        ufile::buildFilePath(strArtefactsPath, vstrArgs[0], strScriptPathName);

        if (!ufile::fileExistsAndNotEmpty(strScriptPathName))
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("Script not found or empty:"); LOG_STRING(strScriptPathName));
            break;
        }

        try
        {
            auto shpDriver = openFn();

            if (shpDriver)
            {
                CommScriptClient<DriverT> client(strScriptPathName, shpDriver, szReadBufferSize, u32ReadTimeout, szDelay);
                bRetVal = client.execute(bIsEnabled);
            }
        }
        catch (const std::bad_alloc& e)
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        }
        catch (const std::exception& e)
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }

    } while (false);

    return bRetVal;
}

} // namespace ucmdexec

#endif // UCOMMANDEXEC_HPP
