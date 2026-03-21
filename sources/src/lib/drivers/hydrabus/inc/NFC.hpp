#ifndef HYDRABUS_NFC_HPP
#define HYDRABUS_NFC_HPP

#include "Protocol.hpp"

namespace HydraHAL {

/**
 * @brief HydraBus NFC Reader binary mode handler.
 *
 * Supports ISO 14443A and ISO 15693 modes.  Provides RF field control,
 * full-byte frame transmission, and partial-bit transmission for
 * anticollision loops.
 *
 * @example
 * @code
 * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::NFC nfc(hb);
 * nfc.set_mode(HydraHAL::NFC::Mode::ISO_14443A);
 * nfc.set_rf(true);                              // RF on
 * nfc.write_bits({0x26}, 7);                     // REQA → ATQA
 * auto atqa = nfc.write({0x93, 0x20}, false);    // Anticol CL1
 * @endcode
 */
class NFC : public Protocol {

public:

    enum class Mode : uint8_t {
        ISO_14443A = 0,
        ISO_15693  = 1,
    };

    explicit NFC(std::shared_ptr<Hydrabus> hydrabus);

    // -------------------------------------------------------------------------
    // RF field
    // -------------------------------------------------------------------------

    /** @return Current RF field state. */
    bool get_rf() const;

    /**
     * @brief Turn the RF field on or off.
     * @param on true = RF on, false = RF off.
     */
    void set_rf(bool on);

    // -------------------------------------------------------------------------
    // Mode
    // -------------------------------------------------------------------------

    /** @return Current modulation mode. */
    Mode get_mode() const;

    /**
     * @brief Select the NFC modulation/protocol mode.
     * @param mode ISO_14443A or ISO_15693.
     */
    void set_mode(Mode mode);

    // -------------------------------------------------------------------------
    // Data transfer
    // -------------------------------------------------------------------------

    /**
     * @brief Send full bytes and receive the response (HydraFW 0b00000101).
     *
     * @param data  Bytes to transmit.
     * @param crc   1 = append CRC, 0 = no CRC.
     * @return Response bytes (length determined by firmware).
     */
    std::vector<uint8_t> write(std::span<const uint8_t> data, bool append_crc = false);

    /**
     * @brief Transmit a partial byte (for anticollision) (HydraFW 0b00000100).
     *
     * @param data     Single byte containing the bits to send (MSB-first).
     * @param num_bits Number of bits to transmit from `data` (1–7).
     * @return Response bytes.
     */
    std::vector<uint8_t> write_bits(uint8_t data, uint8_t num_bits);

private:
    
    Mode _mode {Mode::ISO_14443A};
    bool _rf   {false};
};

} // namespace HydraHAL

#endif //HYDRABUS_NFC_HPP
