#include "net/Flv.hpp"

namespace abyss::net {

namespace {

constexpr std::uint8_t kAvcCodecId = 7;
constexpr std::uint8_t kKeyFrame = 1;
constexpr std::uint8_t kInterFrame = 2;
constexpr std::uint8_t kAacHeader = (10 << 4) | (3 << 2) | (1 << 1) | 1; // AAC, 44 kHz, 16-bit, stereo

Bytes videoTag(std::uint8_t frameType, std::uint8_t packetType, std::int32_t compositionTime, ByteView data)
{
    Bytes out;
    out.reserve(data.size() + 5);
    putU8(out, static_cast<std::uint8_t>((frameType << 4) | kAvcCodecId));
    putU8(out, packetType);
    putU24(out, static_cast<std::uint32_t>(compositionTime) & 0xFFFFFF); // signed 24-bit
    putBytes(out, data);
    return out;
}

Bytes audioTag(std::uint8_t packetType, ByteView data)
{
    Bytes out;
    out.reserve(data.size() + 2);
    putU8(out, kAacHeader);
    putU8(out, packetType);
    putBytes(out, data);
    return out;
}

} // namespace

Bytes flvAvcSequenceHeader(ByteView avcDecoderConfig)
{
    return videoTag(kKeyFrame, 0, 0, avcDecoderConfig);
}

Bytes flvAvcFrame(ByteView avcc, bool keyframe, std::int32_t compositionTimeMs)
{
    return videoTag(keyframe ? kKeyFrame : kInterFrame, 1, compositionTimeMs, avcc);
}

Bytes flvAacSequenceHeader(ByteView audioSpecificConfig)
{
    return audioTag(0, audioSpecificConfig);
}

Bytes flvAacFrame(ByteView rawAac)
{
    return audioTag(1, rawAac);
}

Bytes flvMetadata(const StreamMetadata &m)
{
    Bytes out;
    amf0Write(out, "@setDataFrame");
    amf0Write(out, "onMetaData");
    amf0WriteEcmaArray(out, Amf0Object{
                                {"duration", 0.0},
                                {"width", static_cast<double>(m.width)},
                                {"height", static_cast<double>(m.height)},
                                {"framerate", m.framerate},
                                {"videocodecid", static_cast<double>(kAvcCodecId)},
                                {"videodatarate", static_cast<double>(m.videoKbps)},
                                {"audiocodecid", 10.0},
                                {"audiodatarate", static_cast<double>(m.audioKbps)},
                                {"audiosamplerate", static_cast<double>(m.audioSampleRate)},
                                {"audiosamplesize", 16.0},
                                {"audiochannels", static_cast<double>(m.audioChannels)},
                                {"stereo", m.audioChannels == 2},
                                {"encoder", m.encoder},
                            });
    return out;
}

} // namespace abyss::net
