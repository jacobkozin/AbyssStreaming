#pragma once

#include "net/Amf0.hpp"
#include "net/Bytes.hpp"
#include <cstdint>
#include <string>

namespace abyss::net {

// RTMP audio/video/data messages carry FLV tag bodies. These build them.

// Video tag body: [frame type | codec id 7 (AVC)] [AVC packet type] [composition time] [data].
Bytes flvAvcSequenceHeader(ByteView avcDecoderConfig);
Bytes flvAvcFrame(ByteView avcc, bool keyframe, std::int32_t compositionTimeMs);

// Audio tag body: [AAC, 44 kHz flag, 16-bit, stereo] [AAC packet type] [data].
// (FLV always declares AAC as 44 kHz stereo; the real rate and channel count
// come from the AudioSpecificConfig.)
Bytes flvAacSequenceHeader(ByteView audioSpecificConfig);
Bytes flvAacFrame(ByteView rawAac);

struct StreamMetadata {
    int width{0};
    int height{0};
    double framerate{0};
    int videoKbps{0};
    int audioKbps{0};
    int audioSampleRate{48000};
    int audioChannels{2};
    std::string encoder{"Abyss Stream"};
};
// The "@setDataFrame onMetaData" script message sent after publish starts.
Bytes flvMetadata(const StreamMetadata &metadata);

} // namespace abyss::net
