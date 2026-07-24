#include "TpFactory.hpp"
#include "IsoTpProtocol.hpp"
#include "J1939TpProtocol.hpp"
#include "uLogger.hpp"

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR   "CAN_TP      |"
#define LOG_HDR  LOG_STRING(LT_HDR)

std::unique_ptr<ITransportProtocol> make_transport_protocol(TpProtocol proto, const TpConfig& cfg)
{
    switch (proto)
    {
        case TpProtocol::NONE:
            return nullptr;

        case TpProtocol::ISO_TP:
            return std::make_unique<IsoTpProtocol>(cfg);

        case TpProtocol::J1939_TP:
            return std::make_unique<J1939TpProtocol>(cfg);

        case TpProtocol::CANOPEN_SDO:
            // Not implemented yet — add CanOpenSdoProtocol.hpp/.cpp and a
            // case here when it lands. Falling back to nullptr keeps the
            // caller on the legacy single-frame path instead of crashing.
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("CANOPEN_SDO transport not implemented yet, falling back to raw framing"));
            return nullptr;

        default:
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown TpProtocol value"));
            return nullptr;
    }
}
