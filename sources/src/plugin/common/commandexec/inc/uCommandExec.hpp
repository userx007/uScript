#ifndef UCOMMANDEXEC_HPP
#define UCOMMANDEXEC_HPP

#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uExecContext.hpp"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
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
  *          CYCLIC: parse "time1 val1 [id1], time2 val2 [id2], ..." -> derive a
  *                  master tick (gcd of every time_i) and a super-period
  *                  (lcm of every time_i) -> send whichever entries are due
  *                  at every tick: once per full super-period when launched
  *                  sequentially, or forever (until stop_token fires) when
  *                  launched threaded ('&'). See generic_send_cyclic() below.
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
  * \param[in] pluginName        plugin identity for the GUI comm-dump panel (e.g. UART_PLUGIN_NAME),
  *                                forwarded to CommScriptCommandInterpreter<DriverT>; see gui_notify_comm_dump()
  * \param[in] u32ReadBufferSize  read-buffer size forwarded to CommScriptCommandInterpreter<DriverT>
  * \param[in] u32ReadTimeout    default read timeout forwarded to CommScriptCommandInterpreter<DriverT>
  * \param[in] pszLogHdr         log header literal used to prefix error messages, e.g. "UART        |"
  * \param[out] pstrResultHex    optional; cleared at the start of every call, then - only when the
  *                                command succeeds - overwritten with the hexlified bytes it actually
  *                                received (empty string if the command performed no receive, or
  *                                received nothing before its read timeout elapsed - see
  *                                CommScriptCommandInterpreter::getLastReceived()). Left cleared on
  *                                failure so a hard error never leaks stale/undefined buffer contents.
  *                                Plugins pass their own m_strResultData here so that a "VAL ?= PLUGIN.CMD ..."
  *                                capture picks up whatever was received - most notably the "receive
  *                                whatever is sent" forms ("PLUGIN.CMD <" and "PLUGIN.CMD > ... |" with an
  *                                empty receive side), but this also works for any other receive token type.
  *
  * \return true on success (including the "plugin not enabled" early-out, which validates
  *          arguments without executing), false on a validation or execution failure
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename OpenFn>
bool generic_cmd(const std::string& args,
                  bool bIsEnabled,
                  OpenFn&& openFn,
                  const std::string& pluginName,
                  size_t u32ReadBufferSize,
                  uint32_t u32ReadTimeout,
                  const char* pszLogHdr,
                  std::string* pstrResultHex = nullptr,
                  typename CommScriptCommandInterpreter<typename std::invoke_result_t<OpenFn>::element_type>::SendFunc pfsend = {},
                  typename CommScriptCommandInterpreter<typename std::invoke_result_t<OpenFn>::element_type>::RecvFunc pfrecv = {})
{
    using DriverT = typename std::invoke_result_t<OpenFn>::element_type;

    bool bRetVal = false;

    // Fresh on every call, whether or not this dispatch ends up performing a
    // receive, so a stale value from a previous CMD invocation is never
    // mistaken for this one's result.
    if (pstrResultHex) {
        pstrResultHex->clear();
    }

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
                    CommScriptCommandInterpreter<DriverT> interpreter(shpDriver, pluginName, u32ReadBufferSize, u32ReadTimeout,
                                                                        std::move(pfsend), std::move(pfrecv));
                    // interpretCommand()'s bRealExec parameter decides whether it may
                    // reach the actual send/receive interface: false during a script
                    // dry-run (see uExecContext.hpp), true otherwise. This used to be
                    // fed bIsEnabled (the plugin's own hardware-enabled config flag),
                    // which is unrelated to dry-run and left the parameter effectively
                    // dead - interpretCommand always executed for real.
                    bRetVal = interpreter.interpretCommand(command, !uexec::isDryRun());

                    if (pstrResultHex && bRetVal) {
                        *pstrResultHex = hexutils::stringHexlify(interpreter.getLastReceived());
                    }
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
  * \param[in] pluginName        plugin identity for the GUI comm-dump panel, forwarded to
  *                                CommScriptClient<DriverT>; see generic_cmd()
  * \param[in] strArtefactsPath  base directory scriptpathname is resolved against
  * \param[in] u32ReadBufferSize  read-buffer size forwarded to CommScriptClient<DriverT>
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
                     const std::string& pluginName,
                     const std::string& strArtefactsPath,
                     size_t u32ReadBufferSize,
                     uint32_t u32ReadTimeout,
                     const char* pszLogHdr,
                     typename CommScriptClient<typename std::invoke_result_t<OpenFn>::element_type>::SendFunc pfsend = {},
                     typename CommScriptClient<typename std::invoke_result_t<OpenFn>::element_type>::RecvFunc pfrecv = {})
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
                CommScriptClient<DriverT> client(strScriptPathName, shpDriver, pluginName, u32ReadBufferSize, u32ReadTimeout,
                                                  szDelay, std::move(pfsend), std::move(pfrecv));
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

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief One parsed "val time" entry out of a CYCLIC array (see generic_send_cyclic()).
  *
  *        strVal is intentionally opaque strings not parsed further by this header
*/
/*--------------------------------------------------------------------------------------------------------*/
struct CyclicEntry
{
    std::string strVal;
    uint32_t    u32PeriodMs;
};


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a CYCLIC array argument string into a vector of CyclicEntry.
  *
  *        Grammar: "time1 val1 [id1], time2 val2 [id2], ..., timeN valN [idN]"
  *          - entries are comma-separated
  *          - each entry is 2 or 3 whitespace-separated tokens, in this order:
  *              time_ms (> 0), val, id (optional)
  *          - id is meaningful only to plugins with a per-message destination/address concept
  *            (e.g. CAN id, I2C slave address, "host:port", dest MAC); when absent, the plugin's
  *            sendFn falls back to whatever the plugin's own default destination is (typically
  *            the destination configured via its CONFIG command) - see generic_send_cyclic().
  *            Point-to-point plugins with no such concept (UART, KSPI, DSPKSPI, TCPIP, W5500NET,
  *            ...) never need it at all.
  *          - val/id may be double-quoted to embed whitespace (see tokenizeSpaceQuotesAware)
  *
  * \param[in]  strArray    the raw array argument
  * \param[out] vEntries    parsed entries, cleared first; left in an unspecified state on failure
  * \param[in]  pszLogHdr   log header literal used to prefix error messages
  *
  * \return true if strArray held at least one syntactically valid entry, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
inline bool parseCyclicArray(const std::string& strArray, std::vector<CyclicEntry>& vEntries, const char* pszLogHdr)
{
    vEntries.clear();

    LOG_PRINT(LOG_INFO, LOG_STRING("ARRAY:") LOG_STRING(strArray));

    std::vector<std::string> vGroups;
    ustring::tokenize(strArray, ',', vGroups);

    for(auto i: vGroups)
        LOG_PRINT(LOG_INFO, LOG_STRING(i));

    for (const auto& strGroup : vGroups)
    {
        if (strGroup.empty()) {
            continue; // tolerate a trailing comma, same convention as ARRAY_MACRO parsing
        }

        std::vector<std::string> vTokens;
        ustring::splitAtFirst(strGroup, ':', vTokens);

        if (vTokens.size() != 2)
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr);
                      LOG_STRING("CYCLIC: expected 'time val', got:"); LOG_STRING(strGroup));
            return false;
        }

        uint32_t u32Period = 0U;
        if (!numeric::str2uint32(vTokens[0], u32Period) || (u32Period == 0U))
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr);
                      LOG_STRING("CYCLIC: invalid (or zero) time:"); LOG_STRING(vTokens[0]));
            return false;
        }

        const std::string& strVal = vTokens[1];

        vEntries.push_back(CyclicEntry{ strVal, u32Period });
    }

    if (vEntries.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("CYCLIC: empty array"));
        return false;
    }

    for(auto& i : vEntries){
        LOG_PRINT(LOG_INFO, LOG_STRING(pszLogHdr);LOG_STRING(i.strVal); LOG_UINT32(i.u32PeriodMs));
    }

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Shared body for a plugin's *_CYCLIC command.
  *
  *        Sends one or more periodic commands described by an array of "time:cmd"
  *
  *            time1:cmd1 , time2:cmd2 , ..., timeN:cmdN
  *
  *        The driver is opened once via openFn() (same dependency-injection shape as
  *        generic_cmd()/generic_script() above) and kept open for the whole CYCLIC session —
  *        one physical connection, however many entries/ticks it ends up serving.
  *
  *        A single master timer ticks at the greatest common divisor (gcd) of every time_i
  *        (milliseconds). At every tick, each entry whose own period has elapsed since the
  *        start is (re-)sent through sendFn(driver, id, val) - id being whatever strId
  *        parseCyclicArray() extracted for that entry, or an empty string if that entry's
  *        optional id was omitted (a plugin whose sendFn ignores id entirely, e.g. UART/KSPI,
  *        is unaffected either way; a plugin that does use id, e.g. KVCAN/KI2C/UDP, should
  *        treat an empty id the same way its CMD/SCRIPT commands already treat an absent
  *        override - falling back to whatever destination is configured via CONFIG).
  *
  *        - Launched WITHOUT '&' (sequential): runs for exactly one super-period — the least
  *          common multiple (lcm) of every time_i — then returns. This still sends every entry
  *          at its correct relative cadence at least once, while guaranteeing the command
  *          returns in bounded time, as required of a non-blocking command (see
  *          PluginCommandEntry::bBlocking — CYCLIC is registered with bBlocking=false precisely
  *          so it stays legal to call without '&').
  *        - Launched WITH '&' (threaded): repeats forever, checking st.stop_requested() once
  *          per tick, until the script interpreter requests a stop (end of script, or an
  *          explicit cancellation) — see ScriptInterpreter's jthread dispatch.
  *
  *        Threaded vs. sequential is detected via st.stop_possible(): the stop_token a jthread
  *        hands to doDispatch() is stop_possible()==true; the default-constructed token used
  *        for a sequential (non-'&') dispatch is stop_possible()==false — see IPlugin.hpp's
  *        doDispatch() doc comment. This is different from (and independent of) checking
  *        stop_requested() alone, which stays false forever on a default-constructed token and
  *        so cannot by itself distinguish "not threaded" from "threaded but not yet cancelled".
  *
  *        Tick scheduling is drift-free: every tick's wake-up is an absolute
  *        std::chrono::steady_clock deadline computed once from a fixed session-start
  *        reference point (tpStart + tickIdx * u64Tick), not a relative sleep_for(u64Tick)
  *        re-measured after every iteration. A relative sleep would let each iteration's own
  *        validate/interpret/send latency add on top of the sleep, so the achieved period
  *        would creep past u64Tick and the error would compound tick after tick over a
  *        long-running threaded session. Anchoring to tpStart keeps the average rate exact
  *        regardless of per-tick jitter, while std::this_thread::sleep_until() still fully
  *        blocks the thread whenever a tick isn't running late — so precision improves with
  *        no added CPU load over the previous sleep_for()-based loop.
  *
  * \param[in] args        "time1 val1 [id1], time2 val2 [id2], ..." (see parseCyclicArray())
  * \param[in] bIsEnabled  the plugin's current enabled state
  * \param[in] openFn      callable: () -> std::shared_ptr<DriverT>; returns nullptr (and should
  *                          log why) on failure to open/configure the driver — see generic_cmd()
  * \param[in] pszLogHdr   log header literal used to prefix error messages
  * \param[in] st          stop_token forwarded from doDispatch(), see behaviour above
  * 
  * \return true on success (including the "plugin not enabled" early-out, which validates
  *          arguments without executing), false on a parse error, a driver-open failure, or if
  *          sendFn() ever fails
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename OpenFn>
bool generic_send_cyclic(const std::string& args,
                          bool bIsEnabled,
                          OpenFn&& openFn,
                          const std::string& pluginName,
                          uint32_t u32ReadBufferSize,
                          uint32_t u32ReadTimeout,
                          const char* pszLogHdr,
                          std::stop_token st,
                          typename CommScriptCommandInterpreter<typename std::invoke_result_t<OpenFn>::element_type>::SendFunc pfsend = {},
                          typename CommScriptCommandInterpreter<typename std::invoke_result_t<OpenFn>::element_type>::RecvFunc pfrecv = {})
{
    using DriverT = typename std::invoke_result_t<OpenFn>::element_type;

    bool bRetVal = false;

    do
    {
        if (args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr);
                      LOG_STRING("Missing arg(s): time1:val1, time2:val2, ..."));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (!bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        std::vector<CyclicEntry> vEntries;
        if (!parseCyclicArray(args, vEntries, pszLogHdr))
        {
            break;
        }

        // Master tick = gcd of every period; one full repeating pattern ("super-period") =
        // lcm of every period, i.e. the point every entry is back in phase with tick 0.
        uint64_t u64Tick        = vEntries.front().u32PeriodMs;
        uint64_t u64SuperPeriod = vEntries.front().u32PeriodMs;

        for (size_t i = 1; i < vEntries.size(); ++i)
        {
            u64Tick        = std::gcd(u64Tick, static_cast<uint64_t>(vEntries[i].u32PeriodMs));
            u64SuperPeriod = std::lcm(u64SuperPeriod, static_cast<uint64_t>(vEntries[i].u32PeriodMs));
        }

        if (u64Tick == 0U)
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("CYCLIC: invalid (zero) tick"));
            break;
        }

        const bool     bThreaded  = st.stop_possible();
        const uint64_t u64NrTicks = u64SuperPeriod / u64Tick; // >= 1, ticks in one full pattern

        try
        {
            std::shared_ptr<DriverT> shpDriver = openFn();

            if (!shpDriver)
            {
                break;
            }

            // Parse+validate every entry's command string exactly once, up front,
            // instead of re-parsing the same never-changing strVal on every single
            // tick for the entire (potentially unbounded, threaded '&') CYCLIC
            // session. generic_send_cyclic() is the one caller of
            // CommScriptCommandValidator that repeats the *same* input over and
            // over - CMD/SCRIPT (generic_cmd()/CommScriptClient) each validate a
            // command exactly once already - so this is the one place where
            // re-validating every iteration is pure wasted work: regex/decorator
            // parsing, token classification, and (for F"file.bin" entries) a
            // filesystem stat() call, repeated at every tick of every entry for
            // as long as the session runs.
            //
            // The resulting CommCommand is a small value type (two
            // pair<string,string> plus a couple of enums/ints, see
            // uCommScriptDataTypes.hpp) that interpretCommand() only ever reads,
            // so caching it verbatim is exactly equivalent to re-parsing the same
            // text again - CommCommand carries no state that could go stale
            // between ticks.
            //
            // An entry whose strVal fails validation can never start succeeding
            // later (strVal is fixed for the lifetime of this call), so - matching
            // the previous behaviour, where such an entry's validateCommand() call
            // failed silently on every tick and simply never got interpreted - it
            // is dropped from vResolvedEntries here and permanently skipped. The
            // only observable difference is that validateCommand()'s own
            // LOG_ERROR now fires once up front instead of once per tick forever.
            struct ResolvedCyclicEntry
            {
                CommCommand command;
                uint32_t    u32PeriodMs;
            };

            std::vector<ResolvedCyclicEntry> vResolvedEntries;
            vResolvedEntries.reserve(vEntries.size());

            {
                CommScriptCommandValidator validator;
                for (const auto& sEntry : vEntries)
                {
                    CommCommand command;
                    if (validator.validateCommand(0, sEntry.strVal, command))
                    {
                        vResolvedEntries.push_back(ResolvedCyclicEntry{ std::move(command), sEntry.u32PeriodMs });
                    }
                }
            }

            if (vResolvedEntries.empty())
            {
                LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("CYCLIC: no valid entry to send"));
                break;
            }

            // Likewise, one CommScriptCommandInterpreter is built once for the
            // whole session and reused for every entry at every tick, rather than
            // being constructed and destroyed per-entry per-tick as before. Beyond
            // the constructor/destructor churn itself, CommScriptCommandInterpreter
            // owns a per-instance regex/converted-data cache (m_regexCache /
            // m_dataCache - see uCommScriptCommandInterpreter.hpp) that speeds up
            // repeated R"..."/H"..."/T"..." etc. values; throwing that cache away
            // and rebuilding it from empty on every single tick defeated its whole
            // purpose. A single long-lived instance here lets that cache warm up
            // once and then serve every remaining tick of the session, exactly
            // the same way one CommScriptCommandInterpreter already serves every
            // line of a whole SCRIPT/CMD run.
            CommScriptCommandInterpreter<DriverT> interpreter(shpDriver, pluginName, u32ReadBufferSize, u32ReadTimeout,
                                                                std::move(pfsend), std::move(pfrecv));
            bRetVal = true;

            // Every tick's wake-up deadline is computed as an offset from this single
            // fixed reference point, rather than "now + u64Tick" measured freshly at the
            // end of every iteration. This is what keeps the schedule drift-free: with a
            // relative sleep_for(u64Tick) after each iteration (the previous
            // implementation), the time spent validating/interpreting/sending that
            // iteration's entries is pure extra latency stacked ON TOP of the sleep, so
            // real (non-zero-cost) I/O makes the loop free-run slower than u64Tick and the
            // lag compounds tick after tick — over a long threaded ('&') CYCLIC session
            // this can drift by whole periods. Anchoring every deadline to tpStart instead
            // means each tick's target time is exact regardless of how long previous ticks
            // took, so the achieved average rate converges on exactly u64Tick ms.
            //
            // std::chrono::steady_clock (monotonic, immune to wall-clock adjustments such
            // as NTP steps or DST) is used rather than system_clock, since a CYCLIC session
            // must not jump or stall because the system clock was corrected mid-run.
            const std::chrono::steady_clock::time_point tpStart = std::chrono::steady_clock::now();

            for (uint64_t u64TickIdx = 0; ; ++u64TickIdx)
            {
                const uint64_t u64Elapsed = u64TickIdx * u64Tick;

                for (const auto& sEntry : vResolvedEntries)
                {
                    if ((u64Elapsed % static_cast<uint64_t>(sEntry.u32PeriodMs)) == 0U)
                    {
                        // interpretCommand()'s bRealExec parameter decides whether it may
                        // reach the actual send/receive interface: false during a script
                        // dry-run (see uExecContext.hpp), true otherwise. This used to be
                        // fed bIsEnabled (the plugin's own hardware-enabled config flag),
                        // which is unrelated to dry-run and left the parameter effectively
                        // dead - interpretCommand always executed for real.
                        if(false == (bRetVal = interpreter.interpretCommand(sEntry.command, !uexec::isDryRun()))) {
                            break;
                        }
                    }
                }

                if (!bRetVal)
                {
                    break;
                }

                if (bThreaded)
                {
                    if (st.stop_requested())
                    {
                        break;
                    }
                }
                else if ((u64TickIdx + 1U) >= u64NrTicks)
                {
                    break; // sequential call: one full super-period done, return
                }

                // Absolute deadline for the *next* tick — tpStart + (idx+1) ticks — not a
                // fresh "sleep u64Tick from here" relative wait. If this iteration's work
                // already ran past that point (slow driver, large entry count, ...), the
                // deadline is already in the past: sleep_until() then returns immediately
                // instead of blocking, so the loop catches back up on the very next
                // iteration rather than sleeping a full extra u64Tick and drifting further
                // behind. The thread still fully blocks whenever there IS time left before
                // the deadline (no busy-polling), so steady-state CPU load stays exactly as
                // low as the previous sleep_for() approach — precision improves at zero
                // extra load cost.
                const std::chrono::steady_clock::time_point tpNextTick =
                    tpStart + std::chrono::milliseconds(u64Tick * (u64TickIdx + 1U));
                std::this_thread::sleep_until(tpNextTick);
            }
        }
        catch (const std::bad_alloc& e)
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
            bRetVal = false;
        }
        catch (const std::exception& e)
        {
            LOG_PRINT(LOG_ERROR, LOG_STRING(pszLogHdr); LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
            bRetVal = false;
        }

    } while (false);

    return bRetVal;
}

} // namespace ucmdexec

#endif // UCOMMANDEXEC_HPP
