#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace abyss::net {

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

// Big-endian writers for the RTMP/FLV wire formats.
inline void putU8(Bytes &out, std::uint8_t value) { out.push_back(value); }

inline void putU16(Bytes &out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

inline void putU24(Bytes &out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

inline void putU32(Bytes &out, std::uint32_t value)
{
    putU16(out, static_cast<std::uint16_t>(value >> 16));
    putU16(out, static_cast<std::uint16_t>(value));
}

// RTMP message stream ids are the one little-endian field in the protocol.
inline void putU32LE(Bytes &out, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

inline void putBytes(Bytes &out, ByteView data) { out.insert(out.end(), data.begin(), data.end()); }

inline void putString(Bytes &out, std::string_view text)
{
    out.insert(out.end(), text.begin(), text.end());
}

inline std::uint32_t readU16(const std::uint8_t *p) { return (std::uint32_t{p[0]} << 8) | p[1]; }
inline std::uint32_t readU24(const std::uint8_t *p) { return (std::uint32_t{p[0]} << 16) | (std::uint32_t{p[1]} << 8) | p[2]; }
inline std::uint32_t readU32(const std::uint8_t *p) { return (readU16(p) << 16) | readU16(p + 2); }
inline std::uint32_t readU32LE(const std::uint8_t *p)
{
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
}

inline ByteView view(const Bytes &bytes) { return {bytes.data(), bytes.size()}; }
inline ByteView view(std::string_view text)
{
    return {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()};
}

} // namespace abyss::net
