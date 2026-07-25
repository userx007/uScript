#ifndef CAN_TP_CONFIG_HPP
#define CAN_TP_CONFIG_HPP


/**
 * @brief Tuning parameters shared by the segmented transport protocols.
 *
 * A single struct is used for every protocol so the plugin-side CONFIG/INI
 * surface stays uniform; fields that a given protocol does not use are
 * simply ignored by that protocol's implementation.
 */
struct TpConfig
{
    // ---- ISO-TP (ISO 15765-2) -------------------------------------------------
    uint8_t  blockSize     = 0;     /**< BS sent in our Flow Control frames (0 = no limit). */
    uint8_t  stMin         = 0;     /**< STmin sent in our Flow Control frames (raw encoded byte). */
    bool     padFrames     = true;  /**< Pad SF/CF/FC to 8 bytes (classic CAN convention).  */
    uint8_t  paddingByte   = 0xCC;  /**< Padding fill byte.                                 */
    uint32_t timeoutNBs_ms = 1000;  /**< N_Bs: max wait for Flow Control after our First Frame. */
    uint32_t timeoutNCr_ms = 1000;  /**< N_Cr: max wait for next Consecutive Frame from peer. */
    size_t   maxMessageLen = 4095;  /**< Classic ISO-TP 12-bit length field limit.          */

    // ---- J1939-21 TP ------------------------------------------------------------
    bool     j1939UseBam       = false; /**< true = broadcast (BAM), false = peer-to-peer (RTS/CTS). */
    uint8_t  j1939MaxPackets   = 255;   /**< Max packets we grant per CTS (RTS/CTS only).    */
    uint32_t timeoutT1_ms      = 750;   /**< T1: max wait for CTS after RTS.                 */
    uint32_t timeoutT2_ms      = 1250;  /**< T2: max wait for a data packet after CTS.       */
    uint32_t timeoutT3_ms      = 1250;  /**< T3: max wait for next CTS after a burst.        */
    uint32_t timeoutTh_ms      = 500;   /**< Th (BAM): max inter-packet gap on the receive side. */
    size_t   j1939MaxMessageLen = 1785; /**< J1939-21 message-size limit.                    */

    // ---- CANopen SDO (segmented / block transfer) --------------------------------
    uint16_t canOpenIndex        = 0x2000; /**< Object Dictionary index of the entry being transferred. */
    uint8_t  canOpenSubIndex     = 0x00;   /**< Object Dictionary sub-index.                    */
    bool     canOpenUseBlock     = false;  /**< true = block transfer, false = segmented transfer. */
    uint8_t  canOpenBlockSize    = 127;    /**< Block transfer: segments per block, 1-127.      */
    uint32_t timeoutSdo_ms       = 1000;   /**< Max wait for each SDO response frame.           */
    size_t   canOpenMaxMessageLen = 0xFFFFFFFFu; /**< Upper bound accepted before even trying (32-bit size field). */

    // ---- NMEA 2000 Fast Packet -----------------------------------------------------
    uint32_t timeoutFpInterFrame_ms  = 500; /**< Max gap between consecutive Fast Packet frames. */
    size_t   fastPacketMaxMessageLen = 223; /**< NMEA 2000 Fast Packet payload limit (6 + 31*7). */
};

#endif // CAN_TP_CONFIG_HPP
