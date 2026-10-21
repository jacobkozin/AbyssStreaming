#pragma once

#include "net/Bytes.hpp"
#include <optional>
#include <vector>

namespace abyss::net {

// H.264 helpers for packaging encoder output into FLV/RTMP.
//
// Encoders (NVENC, x264) emit "Annex B" byte streams: NAL units separated by
// 00 00 01 / 00 00 00 01 start codes. FLV instead wants "AVCC": each NAL unit
// prefixed by its 4-byte length, plus a one-off AVCDecoderConfigurationRecord
// holding the SPS and PPS.

enum class NalType : std::uint8_t {
    NonIdrSlice = 1,
    IdrSlice = 5,
    Sei = 6,
    Sps = 7,
    Pps = 8,
    AccessUnitDelimiter = 9,
};

// NAL units of an Annex B buffer, without their start codes.
std::vector<ByteView> splitAnnexB(ByteView annexB);
inline NalType nalType(ByteView nal) { return static_cast<NalType>(nal.empty() ? 0 : nal[0] & 0x1F); }

// Annex B -> length-prefixed (AVCC). Access unit delimiters are dropped;
// FLV players do not need them.
Bytes annexBToAvcc(ByteView annexB);
bool containsIdr(ByteView annexB);

struct AvcParameterSets {
    Bytes sps;
    Bytes pps;
};
// Finds the first SPS and PPS in an encoder's header (extra data).
std::optional<AvcParameterSets> findParameterSets(ByteView annexB);
Bytes buildAvcDecoderConfig(const AvcParameterSets &sets);

struct SpsInfo {
    int width{0};
    int height{0};
    int profileIdc{0};
    int levelIdc{0};
};
std::optional<SpsInfo> parseSps(ByteView sps);

} // namespace abyss::net
