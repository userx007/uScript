#ifndef PCAN_SETUP_HPP
#define PCAN_SETUP_HPP

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

#define LT_HDR     "PCAN_SETUP  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key=value token stream and dispatch each token to the matching PCAN setter.
  *
  * Recognised keys (compatible with kvcan_setup and slcan_setup key naming conventions):
  *   i  –  PCAN channel handle   (calls setPcanChannel)   e.g. "0x51" = PCAN_USBBUS1
  *   b  –  CAN bitrate in bps    (calls setPcanBitrate)   e.g. "500000"
  *   x  –  TX frame ID           (calls setCanTxId)       e.g. "0x7FF" or "0x18DAF100"
  *   y  –  RX frame ID           (calls setCanRxId; defaults to the TX id if never set)
  *   r  –  read timeout (ms)     (calls setCanReadTimeout)
  *   w  –  write timeout (ms)    (calls setCanWriteTimeout)
  *   s  –  read buf size         (calls setCanReadBufferSize)
  *   e  –  force extended IDs    (calls setPcanExtended)  [0=auto, 1=force 29-bit]
  *   f  –  CAN FD mode           (calls setPcanFd)        [0=classic, 1=FD]
  *   t  –  transport protocol    (calls setCanTpProtocol; "none"/"isotp"/"j1939")
  *
  *   TpConfig tuning parameters (see TpConfig.hpp for units/defaults; a key
  *   a given protocol doesn't use is simply ignored by that protocol; same
  *   key set as kvcan_setup.hpp):
  *     bs, stmin, pad, padb, nbs, ncr, maxlen             – ISO-TP
  *     bam, maxpkt, t1, t2, t3, th, jmaxlen               – J1939-21
  *     coidx, cosub, coblk, coblksz, sdotout, comaxlen    – CANopen SDO
  *     fpinter, fpmaxlen                                  – NMEA2000 Fast Packet
  *
  * Unknown keys are silently skipped so that callers adding future keys stay
  * forward-compatible with older setup headers.
  *
  * \param[in] pOwner  pointer to the plugin instance (provides the setPcan setCanXxx methods)
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
    struct Entry { const char *key; Setter setter; };

    // "i" (setPcanChannel) returns void — handled as a special case below so
    // the table itself only needs to hold the uniform bool-returning setters.
    static constexpr Entry table[] = {
        {"b", &T::setPcanBitrate},
        {"x", &T::setCanTxId},
        {"y", &T::setCanRxId},
        {"r", &T::setCanReadTimeout},
        {"w", &T::setCanWriteTimeout},
        {"s", &T::setCanReadBufferSize},
        {"e", &T::setPcanExtended},
        {"f", &T::setPcanFd},
        {"t", &T::setCanTpProtocol},
        // TpConfig tuning parameters
        {"bs",       &T::setTpBlockSize},
        {"stmin",    &T::setTpStMin},
        {"pad",      &T::setTpPadFrames},
        {"padb",     &T::setTpPaddingByte},
        {"nbs",      &T::setTpTimeoutNBs},
        {"ncr",      &T::setTpTimeoutNCr},
        {"maxlen",   &T::setTpMaxMessageLen},
        {"bam",      &T::setJ1939UseBam},
        {"maxpkt",   &T::setJ1939MaxPackets},
        {"t1",       &T::setTpTimeoutT1},
        {"t2",       &T::setTpTimeoutT2},
        {"t3",       &T::setTpTimeoutT3},
        {"th",       &T::setTpTimeoutTh},
        {"jmaxlen",  &T::setJ1939MaxMessageLen},
        {"coidx",    &T::setCanOpenIndex},
        {"cosub",    &T::setCanOpenSubIndex},
        {"coblk",    &T::setCanOpenUseBlock},
        {"coblksz",  &T::setCanOpenBlockSize},
        {"sdotout",  &T::setTpTimeoutSdo},
        {"comaxlen", &T::setCanOpenMaxMessageLen},
        {"fpinter",  &T::setTpTimeoutFpInterFrame},
        {"fpmaxlen", &T::setFpMaxMessageLen},
        {"raw",      &T::setRawResult},
        {"cached",   &T::setCyclicCached},
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
        // macro has a real value yet. Real execution always resolves every
        // $macro before the plugin ever sees the string (see
        // ScriptInterpreter::m_executeCommand()'s real-exec vs. dry-run
        // branches). Accept the key and defer the actual value/range check
        // to real execution — same rationale and behaviour as kvcan_setup.hpp.
        if (!value.empty() && value[0] == '$') {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(value);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

        // "i" key calls a void setter — handle separately
        if (key == "i") {
            pOwner->setPcanChannel(value);
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
 * \brief Apply a set of PCAN parameters expressed as a space-separated key=value string.
 *
 * Intended to back the CONFIG command handler.  The function validates that at
 * least one argument is present, then delegates token parsing to
 * parseAndCallHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance; must implement the
 *                    setPcanChannel/setPcanBitrate/setCanXxx family of setters
 * \param[in] args    space-separated key=value pairs
 *                    (i=channel  b=bitrate  x=tx_id  y=rx_id  r=read_tout  w=write_tout
 *                     s=recv_bufsize  e=extended  f=fd  t=tp_protocol)
 * \return true if processing succeeded, false otherwise
 *
 * NOTE: The owner component must implement the interfaces:
 *  - setPcanChannel
 *  - setPcanBitrate
 *  - setPcanExtended
 *  - setPcanFd
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


#endif // PCAN_SETUP_HPP
