#ifndef CAN_TP_SDO_LOOPBACK_SERVER_HPP
#define CAN_TP_SDO_LOOPBACK_SERVER_HPP

#include "ICommDriver.hpp"
#include <cstdint>
#include <functional>
#include <vector>

/**
 * @file SdoLoopbackServer.hpp
 * @brief Minimal CANopen SDO *server* role, for looping back CanOpenSdoProtocol.
 *
 * CanOpenSdoProtocol::send() (download) and ::receive() (upload) are BOTH
 * client-role operations — "client" is always us, per that class's own doc
 * comment. Neither is the answering side of the handshake, and this library
 * ships no SDO server implementation at all: there is nothing to pair a
 * second CanOpenSdoProtocol instance against the way ISO-TP/J1939/Fast
 * Packet can just run send() on one thread and receive() on another.
 *
 * This class fills that gap *for the loopback app only* — a hand-written,
 * protocol-accurate (not using ITransportProtocol) server that understands
 * just enough of CiA 301 §7.2.4 to answer a real CanOpenSdoProtocol client:
 *   - Initiate Download (expedited and segmented) + Download Segment, storing
 *     into an internal buffer (serve_download()).
 *   - Initiate Upload (expedited and segmented) + Upload Segment, streaming
 *     out of a caller-supplied buffer (serve_upload()).
 *
 * Deliberately out of scope: Block Transfer. CanOpenSdoProtocol::send()'s
 * block-download path has no fallback if the server declines block transfer
 * (unlike its receive()/upload path, which *does* gracefully fall back to
 * segmented) — emulating it correctly would mean either fully implementing
 * block-transfer server state or risking the client hard-failing with
 * PROTOCOL_ERROR. The loopback app therefore always builds its CANopen SDO
 * test with TpConfig::canOpenUseBlock = false.
 */
class SdoLoopbackServer
{
public:
    /**
     * @brief Answers exactly one Download (client send()) transaction.
     * @param bus      Shared loopback bus.
     * @param rxId     Id the client's requests arrive on (== client's txId).
     * @param txId     Id our responses go out on (== client's rxId).
     * @param timeoutMs Deadline for each individual frame wait.
     * @param outData  Receives the bytes the client downloaded, on success.
     * @return true if a full transfer (expedited or segmented) completed.
     */
    static bool serve_download(const ICommDriver &bus, std::string_view rxId, std::string_view txId,
                                uint32_t timeoutMs, std::vector<uint8_t> &outData);

    /**
     * @brief Answers exactly one Upload (client receive()) transaction.
     * @param data  Bytes to serve back to the client, verbatim.
     */
    static bool serve_upload(const ICommDriver &bus, std::string_view rxId, std::string_view txId,
                              uint32_t timeoutMs, const std::vector<uint8_t> &data);

    /**
     * @brief Answers exactly one transaction, Download OR Upload, whichever
     *        the peer initiates — for a real (SocketCAN) bus rather than the
     *        in-memory LoopbackCommDriver serve_download()/serve_upload()
     *        were written for.
     *
     * The reason this needs to exist separately: on a real socket, a frame
     * read off the wire is gone once read — there's no way to "put it back"
     * for a different call to try. serve_download()/serve_upload() each
     * assume their caller already knows which one is about to happen; on a
     * real bus nothing tells the loopback app that in advance, so this
     * function reads the incoming Initiate frame itself, inspects its
     * command specifier to learn the direction, and only then dispatches —
     * without losing that first frame.
     *
     * @param stored             Download: filled with the bytes the client
     *                           downloaded. Upload: bytes served back to the
     *                           client, taken from this same buffer (so a
     *                           caller that Downloads once and then Uploads
     *                           echoes the stored message back unmodified).
     * @param onDirectionKnown   Optional hook invoked as soon as the
     *                           direction is known, before the first
     *                           response frame is sent — e.g. to apply a
     *                           configurable delay before echoing @p stored
     *                           back out on an Upload. Called with true for
     *                           Download, false for Upload.
     * @return true if a full transaction (either direction) completed.
     */
    static bool serve_one(const ICommDriver &bus, std::string_view rxId, std::string_view txId,
                           uint32_t timeoutMs, std::vector<uint8_t> &stored,
                           const std::function<void(bool isDownload)> &onDirectionKnown = {});
};

#endif // CAN_TP_SDO_LOOPBACK_SERVER_HPP
