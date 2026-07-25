#include "TpFactory.hpp"
#include "IsoTpProtocol.hpp"
#include "J1939TpProtocol.hpp"
#include "CanOpenSdoProtocol.hpp"
#include "Nmea2000FastPacketProtocol.hpp"
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
            return std::make_unique<CanOpenSdoProtocol>(cfg);

        case TpProtocol::NMEA2000_FAST_PACKET:
            return std::make_unique<Nmea2000FastPacketProtocol>(cfg);

        default:
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown TpProtocol value"));
            return nullptr;
    }
}
