#ifndef CAN_TP_FACTORY_HPP
#define CAN_TP_FACTORY_HPP

#include "ITransportProtocol.hpp"
#include "TpConfig.hpp"

#include <memory>

/**
 * @file TpFactory.hpp
 * @brief Builds an ITransportProtocol instance for a given TpProtocol enum value.
 *
 * This is the single switch statement the whole library funnels through, so
 * adding another protocol means writing IsoTpProtocol-style .hpp/.cpp files
 * (plus a CMakeLists.txt for its own uXxx target) and adding one case here
 * — no plugin needs to change.
 *
 * @param proto  Which protocol to instantiate. TpProtocol::NONE returns
 *               nullptr: callers should treat that as "use the driver's
 *               raw tout_read()/tout_write() directly, unchanged" rather
 *               than as an error.
 * @param cfg    Tuning parameters (block size, timeouts, ...); irrelevant
 *               fields for the selected protocol are ignored.
 * @return owning pointer to a stateless, reusable protocol instance, or
 *         nullptr for TpProtocol::NONE / not-yet-implemented protocols.
 */
std::unique_ptr<ITransportProtocol> make_transport_protocol(TpProtocol proto, const TpConfig& cfg = {});

#endif // CAN_TP_FACTORY_HPP
