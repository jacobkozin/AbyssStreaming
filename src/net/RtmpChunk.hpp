#pragma once

#include "net/Bytes.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace abyss::net::rtmp {

// RTMP message type ids (RTMP 1.0 spec, sections 5.4 and 7.1).
enum MessageType : std::uint8_t {
    SetChunkSize = 1,
    Abort = 2,
    Acknowledgement = 3,
    UserControl = 4,
    WindowAckSize = 5,
    SetPeerBandwidth = 6,
    Audio = 8,
    Video = 9,
    DataAmf0 = 18,
    CommandAmf0 = 20,
};

// Chunk stream ids Abyss uses when sending.
enum ChunkStream : std::uint32_t {
    ControlStream = 2,
    CommandStream = 3,
    AudioStream = 4,
    VideoStream = 6,
    DataStream = 5,
};

struct Message {
    std::uint8_t type{0};
    std::uint32_t streamId{0};
    std::uint32_t timestamp{0};
    Bytes payload;
};

// Splits messages into chunks. Every message starts with a full (type 0)
// header and continues with 1-byte type 3 headers; header compression is
// optional in the protocol and not worth the complexity here.
class ChunkWriter {
public:
    void setChunkSize(std::uint32_t size) { chunkSize_ = size; }
    [[nodiscard]] std::uint32_t chunkSize() const { return chunkSize_; }
    void write(Bytes &out, std::uint32_t chunkStreamId, std::uint8_t type, std::uint32_t streamId,
               std::uint32_t timestamp, ByteView payload) const;

private:
    std::uint32_t chunkSize_{128};
};

// Reassembles messages from received chunks, handling all four header
// types, extended timestamps and chunk size changes.
class ChunkReader {
public:
    // Largest message accepted from a server; anything bigger is an error.
    static constexpr std::uint32_t kMaxMessageSize = 16 * 1024 * 1024;

    // Appends complete messages to `messages`. Returns false on a protocol
    // error, after which the connection should be dropped.
    bool feed(ByteView data, std::vector<Message> &messages, std::string &error);
    [[nodiscard]] std::uint32_t chunkSize() const { return chunkSize_; }

private:
    struct StreamState {
        std::uint32_t timestamp{0};
        std::uint32_t delta{0};
        std::uint32_t length{0};
        std::uint8_t type{0};
        std::uint32_t streamId{0};
        bool extended{false};
        bool seen{false};
        Bytes partial;
    };

    // Parses one chunk from buffer_ at offset 0. Returns 0 when more bytes
    // are needed, -1 on error, otherwise the bytes consumed.
    long parseChunk(std::vector<Message> &messages, std::string &error);

    Bytes buffer_;
    std::map<std::uint32_t, StreamState> streams_;
    std::uint32_t chunkSize_{128};
};

} // namespace abyss::net::rtmp
