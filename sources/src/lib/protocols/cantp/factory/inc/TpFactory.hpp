#ifndef CAN_TP_FACTORY_HPP
#define CAN_TP_FACTORY_HPP

#include "ITransportProtocol.hpp"
#include "TpConfig.hpp"

#include <memory>

/**
 * @file TpFactory.hpp
 * @brief Builds an ITransportProtocol instance for a given TpProtocol enum value.
 *
 * @param proto  Which protocol to instantiate. TpProtocol::NONE returns
 *               nullptr: callers should treat that as "use the driver's
 *               raw tout_read()/tout_write() directly" rather than as an error.
 * @param cfg    Tuning parameters (block size, timeouts, ...); fields not
 *               used by the selected protocol are ignored.
 * @return owning pointer to a stateless, reusable protocol instance, or
 *         nullptr for TpProtocol::NONE.
 */
std::unique_ptr<ITransportProtocol> make_transport_protocol(TpProtocol proto, const TpConfig& cfg = {});

#endif // CAN_TP_FACTORY_HPP
