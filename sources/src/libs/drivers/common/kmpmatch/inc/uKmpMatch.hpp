#ifndef UKMPMATCH_HPP
#define UKMPMATCH_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

/**
  * \brief Shared Knuth-Morris-Pratt (KMP) token-matching helpers used by the
  *        byte/frame/datagram-oriented comm drivers (UART, KI2C, KSPI, CH341,
  *        TCPIP, UDP, KVCAN, RawEth, ...).
  *
  *        Every one of these drivers implements the same "wait until a token
  *        byte-sequence is seen on the wire" primitive: build a KMP failure
  *        table for the token once, then stream bytes in - one at a time for
  *        byte-oriented transports, or a chunk/frame/datagram at a time for
  *        packet-oriented transports - and run them through the KMP state
  *        machine until the token is matched or a read fails/times out.
  *
  *        That logic used to be copy-pasted, algorithm and all, into each
  *        driver's *Common.cpp. This header factors it out so a fix or an
  *        improvement only needs to be made once. Each driver still owns two
  *        thin private member functions (build_kmp_table / kmp_stream_match)
  *        that simply delegate here - the public API and each driver's own
  *        Status enum type are untouched.
*/
namespace ukmp
{

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Build the KMP partial-match (failure function) table for a pattern.
  *
  *        Pure algorithm, no I/O - identical for every driver, so unlike
  *        kmp_stream_match() below it needs no per-driver customisation at all.
  *
  * \param[in]  pattern   the token to search for
  * \param[in]  szLength  number of bytes of pattern to consider (== pattern.size() in every caller)
  * \param[out] viLps     resized to szLength and filled with the failure-function values
*/
/*--------------------------------------------------------------------------------------------------------*/
inline void build_kmp_table(std::span<const uint8_t> pattern, size_t szLength, std::vector<int>& viLps)
{
    viLps.resize(szLength);
    int len  = 0;
    viLps[0] = 0;

    for (size_t i = 1; i < szLength; )
    {
        if (pattern[i] == pattern[len])
        {
            viLps[i++] = ++len;
        }
        else
        {
            if (len != 0)
            {
                len = viLps[len - 1];
            }
            else
            {
                viLps[i++] = 0;
            }
        }
    }
}

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Stream-match a token against bytes pulled from readFn, using the KMP
  *        state machine driven by a pre-built failure table.
  *
  *        readFn is called exactly the way each driver's own timeout_read()
  *        is: readFn(timeout, span<uint8_t> scratch, size_t& bytesRead) -> Status.
  *        Byte-oriented drivers (UART, KI2C, KSPI, CH341) pass a 1-byte
  *        scratch buffer, matching their original one-byte-at-a-time reads;
  *        packet-oriented drivers (TCPIP, UDP, KVCAN, RawEth) pass their
  *        natural chunk/datagram/frame-payload size and this function walks
  *        every byte of each chunk through the state machine - the KMP match
  *        doesn't care whether a chunk boundary splits the token.
  *
  *        The Status type is deduced from readFn's return type, so this
  *        works unmodified against every driver's own (distinct) Status
  *        enum; it only requires Status::SUCCESS and Status::READ_TIMEOUT to
  *        exist, which they do in every driver in this codebase.
  *
  * \param[in] readFn             callable: (uint32_t timeout, std::span<uint8_t> buf, size_t& bytesRead) -> Status
  * \param[in] token              the byte sequence to search for
  * \param[in] viLps              the KMP failure table for token (see build_kmp_table)
  * \param[in] u32Timeout         per-read timeout forwarded to readFn
  * \param[in] bReturnOnTimeout   if true, a timed-out read returns Status::READ_TIMEOUT instead of Status::READ_ERROR
  * \param[in] useBuffer          if true, every byte read is also copied into an internal ring buffer
  *                                (kept only for behavioural parity with the original per-driver code -
  *                                 none of the original implementations ever read this buffer back either)
  * \param[in] szChunkBufferSize  size of the scratch buffer passed to readFn per call (1 for byte-oriented drivers)
  * \param[in] szRingBufferSize   size of the internal ring buffer used when useBuffer is true
  *
  * \return Status::SUCCESS once token is fully matched, Status::READ_TIMEOUT / Status::READ_ERROR on read failure
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename ReaderFn>
auto kmp_stream_match(ReaderFn&& readFn,
                       std::span<const uint8_t> token,
                       const std::vector<int>& viLps,
                       uint32_t u32Timeout,
                       bool bReturnOnTimeout,
                       bool useBuffer,
                       size_t szChunkBufferSize,
                       size_t szRingBufferSize)
    -> std::invoke_result_t<ReaderFn, uint32_t, std::span<uint8_t>, size_t&>
{
    using Status = std::invoke_result_t<ReaderFn, uint32_t, std::span<uint8_t>, size_t&>;

    std::vector<uint8_t> chunk(szChunkBufferSize > 0 ? szChunkBufferSize : 1);
    std::vector<uint8_t> ring(useBuffer ? szRingBufferSize : 0);
    uint32_t u32Matched   = 0;
    uint32_t u32BufferPos = 0;

    while (true)
    {
        size_t szBytesRead = 0;
        const Status readResult = readFn(u32Timeout, std::span<uint8_t>(chunk.data(), chunk.size()), szBytesRead);

        if (readResult != Status::SUCCESS || szBytesRead == 0)
        {
            return (readResult == Status::READ_TIMEOUT && bReturnOnTimeout)
                   ? Status::READ_TIMEOUT
                   : Status::READ_ERROR;
        }

        for (size_t byteIdx = 0; byteIdx < szBytesRead; ++byteIdx)
        {
            const uint8_t cByte = chunk[byteIdx];

            if (useBuffer)
            {
                ring[u32BufferPos++ % szRingBufferSize] = cByte;
            }

            while (u32Matched > 0 && cByte != token[u32Matched])
            {
                u32Matched = static_cast<uint32_t>(viLps[u32Matched - 1]);
            }

            if (cByte == token[u32Matched])
            {
                ++u32Matched;
                if (u32Matched == token.size())
                {
                    return Status::SUCCESS;
                }
            }
        }
    }
}

} // namespace ukmp

#endif // UKMPMATCH_HPP
