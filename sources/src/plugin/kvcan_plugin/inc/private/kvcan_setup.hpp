#ifndef KVCAN_SETUP_HPP
#define KVCAN_SETUP_HPP

#include "uSharedConfig.hpp"
#include "uLogger.hpp"
#include "uString.hpp"

#include <string>
#include <unordered_map>
#include <functional>
#include <sstream>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "KVCAN SETUP |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key=value token stream and dispatch each token to the matching CAN setter.
  *
  * Recognised keys:
  *   i  –  interface name  (calls setCanIface)
  *   x  –  TX frame ID     (calls setCanTxId)
  *   y  –  RX frame ID     (calls setCanRxId; defaults to the TX id if never set)
  *   r  –  read timeout    (calls setCanReadTimeout)
  *   w  –  write timeout   (calls setCanWriteTimeout)
  *   s  –  read buf size   (calls setCanReadBufferSize)
  *   t  –  transport proto (calls setCanTpProtocol; "none"/"isotp"/"j1939")
  *
  *   TpConfig tuning parameters (see TpConfig.hpp for units/defaults; a key
  *   a given protocol doesn't use is simply ignored by that protocol):
  *     bs       – ISO-TP block size             (setTpBlockSize)
  *     stmin    – ISO-TP STmin                  (setTpStMin)
  *     pad      – ISO-TP pad frames to 8 bytes  (setTpPadFrames)
  *     padb     – ISO-TP padding byte           (setTpPaddingByte)
  *     nbs      – ISO-TP N_Bs timeout (ms)      (setTpTimeoutNBs)
  *     ncr      – ISO-TP N_Cr timeout (ms)      (setTpTimeoutNCr)
  *     maxlen   – ISO-TP max message length     (setTpMaxMessageLen)
  *     bam      – J1939-21 use BAM              (setJ1939UseBam)
  *     maxpkt   – J1939-21 max packets per CTS  (setJ1939MaxPackets)
  *     t1       – J1939-21 T1 timeout (ms)      (setTpTimeoutT1)
  *     t2       – J1939-21 T2 timeout (ms)      (setTpTimeoutT2)
  *     t3       – J1939-21 T3 timeout (ms)      (setTpTimeoutT3)
  *     th       – J1939-21 Th timeout (ms)      (setTpTimeoutTh)
  *     jmaxlen  – J1939-21 max message length   (setJ1939MaxMessageLen)
  *     coidx    – CANopen SDO OD index          (setCanOpenIndex)
  *     cosub    – CANopen SDO OD sub-index      (setCanOpenSubIndex)
  *     coblk    – CANopen SDO use block xfer    (setCanOpenUseBlock)
  *     coblksz  – CANopen SDO block size        (setCanOpenBlockSize)
  *     sdotout  – CANopen SDO response timeout  (setTpTimeoutSdo)
  *     comaxlen – CANopen SDO max message length(setCanOpenMaxMessageLen)
  *     fpinter  – NMEA2000 FP inter-frame gap   (setTpTimeoutFpInterFrame)
  *     fpmaxlen – NMEA2000 FP max message length(setFpMaxMessageLen)
  *
  * Unknown keys are silently skipped so that callers adding future keys stay
  * forward-compatible with older setup headers. A value of the form "$name"
  * or "$name.SIZE" is likewise accepted without being parsed/range-checked —
  * see the "$" guard below for why (in short: during script validation the
  * real value isn't known yet).
  *
  * \param[in] pOwner  pointer to the plugin instance (provides the setCan* methods)
  * \param[in] input   space-separated list of "key=value" tokens
  * \return true if every recognised key was accepted by its setter, false on first failure
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool parseAndCallHandlers(const T *pOwner, const std::string& input)
{
    // Static table of (key, member-function-pointer) pairs.
    // Built once at program start; zero heap allocation per call.
    using Setter = bool (T::*)(const std::string&) const;
    struct Entry { const char *key; Setter setter; bool voidReturn; };

    // "i" (setCanIface) returns void — wrap the call so the table stays uniform.
    // We handle it as a special case flagged by voidReturn.
    static constexpr Entry table[] = {
        {"x", &T::setCanTxId,          false},
        {"y", &T::setCanRxId,          false},
        {"r", &T::setCanReadTimeout,   false},
        {"w", &T::setCanWriteTimeout,  false},
        {"s", &T::setCanReadBufferSize,false},
        {"t", &T::setCanTpProtocol,    false},
        // TpConfig tuning parameters
        {"bs",       &T::setTpBlockSize,          false},
        {"stmin",    &T::setTpStMin,              false},
        {"pad",      &T::setTpPadFrames,          false},
        {"padb",     &T::setTpPaddingByte,        false},
        {"nbs",      &T::setTpTimeoutNBs,         false},
        {"ncr",      &T::setTpTimeoutNCr,         false},
        {"maxlen",   &T::setTpMaxMessageLen,      false},
        {"bam",      &T::setJ1939UseBam,          false},
        {"maxpkt",   &T::setJ1939MaxPackets,      false},
        {"t1",       &T::setTpTimeoutT1,          false},
        {"t2",       &T::setTpTimeoutT2,          false},
        {"t3",       &T::setTpTimeoutT3,          false},
        {"th",       &T::setTpTimeoutTh,          false},
        {"jmaxlen",  &T::setJ1939MaxMessageLen,   false},
        {"coidx",    &T::setCanOpenIndex,         false},
        {"cosub",    &T::setCanOpenSubIndex,      false},
        {"coblk",    &T::setCanOpenUseBlock,      false},
        {"coblksz",  &T::setCanOpenBlockSize,     false},
        {"sdotout",  &T::setTpTimeoutSdo,         false},
        {"comaxlen", &T::setCanOpenMaxMessageLen, false},
        {"fpinter",  &T::setTpTimeoutFpInterFrame,false},
        {"fpmaxlen", &T::setFpMaxMessageLen,      false},
    };

    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    while (stream >> token) {
        const auto delimiterPos = token.find(CHAR_SEPARATOR_EQUAL);
        if (delimiterPos == std::string::npos) { continue; }

        const std::string key   = token.substr(0, delimiterPos);
        const std::string value = token.substr(delimiterPos + 1);

        // A value that still starts with '$' is an unexpanded "$macroname"
        // (or "$macroname.SIZE") reference — this call is happening during
        // script VALIDATION (a dry run), before the referenced variable
        // macro has a real value yet. See ScriptInterpreter::m_executeCommand()'s
        // dry-run branch: unlike real execution (which always resolves every
        // $macro via m_replaceVariableMacros() before the plugin ever sees
        // the string), the dry-run dispatch deliberately passes strParams
        // through unexpanded, so a CONFIG/CMD plugin can still validate
        // syntax and open/configure its driver. There is nothing to range-
        // check yet in that case: accept the key and defer the actual
        // value/range check to real execution, when this setter will see
        // the already-resolved literal instead of "$...".
        if (!value.empty() && value[0] == '$') {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(value);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

        // "i" key calls a void setter — handle separately
        if (key == "i") {
            pOwner->setCanIface(value);
            continue;
        }

        for (const auto& entry : table) {
            if (key == entry.key) {
                if (false == (pOwner->*entry.setter)(value)) {
                    bRetVal = false;
                }
                break;
            }
        }

        if (!bRetVal) { break; }
    }
    return bRetVal;

} /* parseAndCallHandlers() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of CAN parameters expressed as a space-separated key=value string.
 *
 * Intended to back the CONFIG command handler.  The function validates that at
 * least one argument is present and that the plugin is in a state where live
 * reconfiguration is meaningful, then delegates token parsing to
 * parseAndCallHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance; must implement isEnabled()
 *                    and the setCan* family of setters
 * \param[in] args    space-separated key=value pairs
 *                    (i=iface  x=tx_id  y=rx_id  r=read_tout  w=write_tout
 *                     s=recv_bufsize  t=tp_protocol)
 * \return true if processing succeeded, false otherwise
 *
 * NOTE: The owner component must implement the interfaces:
 *  - isEnabled
 *  - setCanIface
 *  - setCanTxId
 *  - setCanRxId
 *  - setCanReadTimeout
 *  - setCanWriteTimeout
 *  - setCanReadBufferSize
 *  - setCanTpProtocol
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_can_set_params (const T *pOwner, const std::string &args)
{
    bool bRetVal = false;

    do {

        // no args provided
        if (true == args.empty())
        {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Missing args"));
            break;
        }

        bRetVal = parseAndCallHandlers(pOwner, args);

    } while(false);

    return bRetVal;

} /* generic_can_set_params() */


#endif // KVCAN_SETUP_HPP