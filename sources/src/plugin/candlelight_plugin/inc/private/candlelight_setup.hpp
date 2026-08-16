#ifndef CANDLELIGHT_SETUP_HPP
#define CANDLELIGHT_SETUP_HPP

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

#define LT_HDR     "CANDLE_SETUP|"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key=value token stream and dispatch each token to the matching Candlelight setter.
  *
  * Recognised keys:
  *   vid –  USB vendor id (hex)         (calls setUsbVid)
  *   pid –  USB product id (hex)        (calls setUsbPid)
  *   idx –  Nth matching device to open (calls setUsbDeviceIndex; default 0)
  *   b   –  nominal bit rate, bit/s     (calls setCanBitrate; e.g. 500000)
  *   sp  –  nominal sample point 0-1    (calls setCanSamplePoint; default 0.875)
  *   fb  –  CAN-FD data-phase bit rate  (calls setCanFdBitrate; e.g. 2000000)
  *   fp  –  CAN-FD data sample point    (calls setCanFdSamplePoint; default 0.75)
  *   z   –  CAN-FD BRS                  (calls setCanFdBrs)       [0=off,1=on]
  *   m   –  GS_CAN_MODE_* bitmask       (calls setCanModeFlags)   [see uCandlelight.hpp]
  *   x   –  TX frame ID                 (calls setCanTxId)
  *   v   –  RX frame ID                 (calls setCanRxId; defaults to the TX id if never set)
  *   r   –  read timeout                (calls setCanReadTimeout)
  *   w   –  write timeout               (calls setCanWriteTimeout)
  *   s   –  read buf size                (calls setCanReadBufferSize)
  *   t   –  transport protocol          (calls setCanTpProtocol; "none"/"isotp"/"j1939")
  *
  *   Raw bit-timing override (power users — see uCandlelight.hpp's
  *   GsDeviceBittiming; bypasses the b=/sp= auto-calculation entirely when
  *   any of these is present; all five must be given together):
  *     ps  – prop_seg     (calls setCanPropSeg)
  *     p1  – phase_seg1   (calls setCanPhaseSeg1)
  *     p2  – phase_seg2   (calls setCanPhaseSeg2)
  *     sw  – sjw          (calls setCanSjw)
  *     bp  – brp          (calls setCanBrp)
  *   ... and the CAN-FD data-phase equivalents:
  *     dps, dp1, dp2, dsw, dbp   (calls setCanFdPropSeg/PhaseSeg1/PhaseSeg2/Sjw/Brp)
  *
  *   TpConfig tuning parameters (see TpConfig.hpp for units/defaults; a key
  *   a given protocol doesn't use is simply ignored by that protocol; same
  *   key set as kvcan_setup.hpp/pcan_setup.hpp/ucan_setup.hpp):
  *     bs, stmin, pad, padb, nbs, ncr, maxlen             – ISO-TP
  *     bam, maxpkt, t1, t2, t3, th, jmaxlen               – J1939-21
  *     coidx, cosub, coblk, coblksz, sdotout, comaxlen    – CANopen SDO
  *     fpinter, fpmaxlen                                  – NMEA2000 Fast Packet
  *
  *   raw – skip hexlification of CMD's captured result    (calls setRawResult)
  *
  * Unknown keys are silently skipped so that callers adding future keys stay
  * forward-compatible with older setup headers.
  *
  * \param[in] pOwner  pointer to the plugin instance (provides the setUsb.../setCan... methods)
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

    static constexpr Entry table[] = {
        {"vid",      &T::setUsbVid},
        {"pid",      &T::setUsbPid},
        {"idx",      &T::setUsbDeviceIndex},
        {"b",        &T::setCanBitrate},
        {"sp",       &T::setCanSamplePoint},
        {"fb",       &T::setCanFdBitrate},
        {"fp",       &T::setCanFdSamplePoint},
        {"z",        &T::setCanFdBrs},
        {"m",        &T::setCanModeFlags},
        {"x",        &T::setCanTxId},
        {"v",        &T::setCanRxId},
        {"r",        &T::setCanReadTimeout},
        {"w",        &T::setCanWriteTimeout},
        {"s",        &T::setCanReadBufferSize},
        {"t",        &T::setCanTpProtocol},
        {"ps",       &T::setCanPropSeg},
        {"p1",       &T::setCanPhaseSeg1},
        {"p2",       &T::setCanPhaseSeg2},
        {"sw",       &T::setCanSjw},
        {"bp",       &T::setCanBrp},
        {"dps",      &T::setCanFdPropSeg},
        {"dp1",      &T::setCanFdPhaseSeg1},
        {"dp2",      &T::setCanFdPhaseSeg2},
        {"dsw",      &T::setCanFdSjw},
        {"dbp",      &T::setCanFdBrp},
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
 * \brief Apply a set of Candlelight parameters expressed as a space-separated key=value string.
 *
 * Intended to back the CONFIG command handler.  The function validates that at
 * least one argument is present, then delegates token parsing to
 * parseAndCallHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance; must implement the
 *                    setUsb.../setCan... family of setters
 * \param[in] args    space-separated key=value pairs — see parseAndCallHandlers()'s doc comment
 * \return true if processing succeeded, false otherwise
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


#endif // CANDLELIGHT_SETUP_HPP
