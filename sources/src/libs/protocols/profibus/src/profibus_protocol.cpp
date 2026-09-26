#include "profibus_protocol.hpp"

#include <stddef.h>

// -----------------------------------------------------------------------
// FCS (Frame Check Sequence) — simple 8-bit arithmetic sum, no carry.
// See class doc comment / PROFIBUS Manual "Checksum" page.
// -----------------------------------------------------------------------

uint8_t ProfibusProtocol::computeFcs(uint8_t u8Da, uint8_t u8Sa, uint8_t u8Fc, const std::vector<uint8_t> &vDu)
{
    uint8_t sum = static_cast<uint8_t>(u8Da + u8Sa + u8Fc);
    for (uint8_t b : vDu) {
        sum = static_cast<uint8_t>(sum + b);
    }
    return sum;
}

// -----------------------------------------------------------------------
// FC (Frame Control) construction / decoding
// -----------------------------------------------------------------------

uint8_t ProfibusProtocol::buildRequestFc(uint8_t u8Function, bool bFcb, bool bFcv)
{
    uint8_t fc = 0x40; // bit6 = 1: Request telegram (bit7 Res stays 0)
    if (bFcb) {
        fc |= 0x20;
    }
    if (bFcv) {
        fc |= 0x10;
    }
    fc |= static_cast<uint8_t>(u8Function & 0x0F);
    return fc;
}

ProfibusProtocol::ResponseFc ProfibusProtocol::decodeResponseFc(uint8_t u8Fc)
{
    ResponseFc result;
    result.isRequestFrame = (u8Fc & 0x40) != 0;
    result.stationType    = static_cast<uint8_t>((u8Fc >> 4) & 0x03);
    result.statusCode     = static_cast<uint8_t>(u8Fc & 0x0F);
    return result;
}

const char *ProfibusProtocol::responseStatusName(uint8_t u8StatusCode)
{
    switch (u8StatusCode) {
    case kRspOk:
        return "OK";
    case kRspUserError:
        return "USER_ERROR";
    case kRspNoResources:
        return "NO_RESOURCES";
    case kRspSapNotEnabled:
        return "SAP_NOT_ENABLED";
    case kRspDataLow:
        return "DATA_LOW";
    case kRspNoResponseData:
        return "NO_RESPONSE_DATA";
    case kRspDataHigh:
        return "DATA_HIGH_DIAG_PENDING";
    case kRspDataNotReceivedLow:
        return "DATA_NOT_RECEIVED_LOW";
    case kRspDataNotReceivedHigh:
        return "DATA_NOT_RECEIVED_HIGH";
    default:
        return "RESERVED";
    }
}

// -----------------------------------------------------------------------
// FCB/FCV security sequence (PROFIBUS Manual, "Function code" > "Frame
// Count Bit"). See this function's doc comment in profibus_protocol.hpp.
// -----------------------------------------------------------------------

std::pair<bool, bool> ProfibusProtocol::m_NextFcbFcv(uint8_t u8Da)
{
    auto it = m_lastFcbForDa.find(u8Da);
    if (it == m_lastFcbForDa.end()) {
        // First request to this DA on this session: FCV=0, FCB=1.
        m_lastFcbForDa.emplace(u8Da, true);
        return {true, false};
    }
    // Subsequent request: FCV=1, FCB toggled from the last value sent to
    // this same DA.
    const bool newFcb = !it->second;
    it->second        = newFcb;
    return {newFcb, true};
}

// -----------------------------------------------------------------------
// Telegram assembly
// -----------------------------------------------------------------------

std::vector<uint8_t> ProfibusProtocol::m_BuildDataTelegram(uint8_t u8Da, uint8_t u8Sa, uint8_t u8Fc, const std::vector<uint8_t> &vData)
{
    if (vData.empty()) {
        // SD1 — no vData field.
        std::vector<uint8_t> t;
        t.reserve(6);
        t.push_back(kSD1);
        t.push_back(u8Da);
        t.push_back(u8Sa);
        t.push_back(u8Fc);
        t.push_back(computeFcs(u8Da, u8Sa, u8Fc, {}));
        t.push_back(kED);
        return t;
    }

    if (vData.size() == 8) {
        // SD3 — fixed 8-byte vData field.
        std::vector<uint8_t> t;
        t.reserve(14);
        t.push_back(kSD3);
        t.push_back(u8Da);
        t.push_back(u8Sa);
        t.push_back(u8Fc);
        t.insert(t.end(), vData.begin(), vData.end());
        t.push_back(computeFcs(u8Da, u8Sa, u8Fc, vData));
        t.push_back(kED);
        return t;
    }

    // SD2 — variable-length vData field. LE/LEr count DA+SA+FC+DU (i.e.
    // 3 + vData.size()), per the PROFIBUS Manual's "Length" page; repeated
    // twice (LE, LEr) and the SD2 marker itself repeated, both purely for
    // the receiver's plausibility check (Hamming-distance robustness of
    // the header) — not a length-prefix-plus-checksum scheme.
    const uint8_t le = static_cast<uint8_t>(3 + vData.size());
    std::vector<uint8_t> t;
    t.reserve(static_cast<size_t>(6) + vData.size());
    t.push_back(kSD2);
    t.push_back(le);
    t.push_back(le);
    t.push_back(kSD2);
    t.push_back(u8Da);
    t.push_back(u8Sa);
    t.push_back(u8Fc);
    t.insert(t.end(), vData.begin(), vData.end());
    t.push_back(computeFcs(u8Da, u8Sa, u8Fc, vData));
    t.push_back(kED);
    return t;
}

std::vector<uint8_t> ProfibusProtocol::buildSdn(uint8_t u8Da, uint8_t u8Sa, const std::vector<uint8_t> &vData, bool bHighPriority) const
{
    const uint8_t fc = buildRequestFc(bHighPriority ? kFnSdnHigh : kFnSdnLow, /*fcb=*/false, /*fcv=*/false);
    return m_BuildDataTelegram(u8Da, u8Sa, fc, vData);
}

std::vector<uint8_t> ProfibusProtocol::buildSda(uint8_t u8Da, uint8_t u8Sa, const std::vector<uint8_t> &vData, bool bHighPriority)
{
    const auto [fcb, fcv] = m_NextFcbFcv(u8Da);
    const uint8_t fc      = buildRequestFc(bHighPriority ? kFnSdaHigh : kFnSdaLow, fcb, fcv);
    return m_BuildDataTelegram(u8Da, u8Sa, fc, vData);
}

std::vector<uint8_t> ProfibusProtocol::buildSrd(uint8_t u8Da, uint8_t u8Sa, const std::vector<uint8_t> &vData, bool bHighPriority)
{
    const auto [fcb, fcv] = m_NextFcbFcv(u8Da);
    const uint8_t fc      = buildRequestFc(bHighPriority ? kFnSrdHigh : kFnSrdLow, fcb, fcv);
    return m_BuildDataTelegram(u8Da, u8Sa, fc, vData);
}

std::vector<uint8_t> ProfibusProtocol::buildFdlStatusRequest(uint8_t u8Da, uint8_t u8Sa, bool bHighPriority) const
{
    // Excluded from the security sequence (FCB=FCV=0 always) — see the
    // PROFIBUS Manual's Frame Count Bit table, "Request FDL Status/ Ident/
    // LSAP Status" row.
    (void)bHighPriority; // Request FDL Status has no low/high-priority variant in the function-code table
    const uint8_t fc = buildRequestFc(kFnRequestFdlStatus, /*fcb=*/false, /*fcv=*/false);
    return m_BuildDataTelegram(u8Da, u8Sa, fc, {});
}

// -----------------------------------------------------------------------
// Decoding
// -----------------------------------------------------------------------

ProfibusProtocol::TelegramKind ProfibusProtocol::classifyStartDelimiter(uint8_t u8Sd)
{
    switch (u8Sd) {
    case kSD1:
        return TelegramKind::SD1;
    case kSD2:
        return TelegramKind::SD2;
    case kSD3:
        return TelegramKind::SD3;
    case kSD4:
        return TelegramKind::SD4;
    case kSC:
        return TelegramKind::SC;
    default:
        return TelegramKind::Malformed;
    }
}

ProfibusProtocol::DecodedTelegram ProfibusProtocol::decodeTelegram(const std::vector<uint8_t> &vRaw)
{
    DecodedTelegram result;
    if (vRaw.empty()) {
        return result; // Malformed
    }

    result.kind = classifyStartDelimiter(vRaw[0]);

    switch (result.kind) {
    case TelegramKind::SC:
        result.fcsOk = true; // SC carries no checksum — a single valid byte is the whole telegram
        return result;

    case TelegramKind::SD4:
        // Token telegram: SD4 DA SA, no FCS/ED. Recognised for passive
        // bus monitoring (see ProfibusDriver's standalone receive) but
        // never built by this class — see class doc comment.
        if (vRaw.size() != 3) {
            result.kind = TelegramKind::Malformed;
            return result;
        }
        result.da    = vRaw[1];
        result.sa    = vRaw[2];
        result.fcsOk = true; // no checksum field to verify
        return result;

    case TelegramKind::SD1:
        if (vRaw.size() != 6 || vRaw[5] != kED) {
            result.kind = TelegramKind::Malformed;
            return result;
        }
        result.da    = vRaw[1];
        result.sa    = vRaw[2];
        result.fc    = vRaw[3];
        result.fcsOk = (vRaw[4] == computeFcs(result.da, result.sa, result.fc, {}));
        return result;

    case TelegramKind::SD3:
        if (vRaw.size() != 14 || vRaw[13] != kED) {
            result.kind = TelegramKind::Malformed;
            return result;
        }
        result.da = vRaw[1];
        result.sa = vRaw[2];
        result.fc = vRaw[3];
        result.du.assign(vRaw.begin() + 4, vRaw.begin() + 12);
        result.fcsOk = (vRaw[12] == computeFcs(result.da, result.sa, result.fc, result.du));
        return result;

    case TelegramKind::SD2: {
        // SD2 LE LEr SD2 DA SA FC DU[LE-3] FCS ED — minimum LE is 3
        // (DA+SA+FC, empty DU), maximum data length is 246 (LE<=249).
        if (vRaw.size() < 9) {
            result.kind = TelegramKind::Malformed;
            return result;
        }
        const uint8_t le  = vRaw[1];
        const uint8_t ler = vRaw[2];
        if (le != ler || vRaw[3] != kSD2 || le < 3) {
            result.kind = TelegramKind::Malformed;
            return result;
        }
        const size_t duLen         = static_cast<size_t>(le) - 3;
        const size_t expectedTotal = 4 /*SD2,LE,LEr,SD2*/ + 3 /*DA,SA,FC*/ + duLen + 2 /*FCS,ED*/;
        if (vRaw.size() != expectedTotal || vRaw[vRaw.size() - 1] != kED) {
            result.kind = TelegramKind::Malformed;
            return result;
        }
        result.da = vRaw[4];
        result.sa = vRaw[5];
        result.fc = vRaw[6];
        result.du.assign(vRaw.begin() + 7, vRaw.begin() + 7 + static_cast<long>(duLen));
        result.fcsOk = (vRaw[7 + duLen] == computeFcs(result.da, result.sa, result.fc, result.du));
        return result;
    }

    case TelegramKind::Malformed:
    default:
        return result;
    }
}
