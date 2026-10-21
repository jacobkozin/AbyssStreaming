#include "net/RtmpChunk.hpp"

#include <algorithm>

namespace abyss::net::rtmp {

namespace {

constexpr std::uint32_t kExtendedTimestamp = 0xFFFFFF;

void putBasicHeader(Bytes &out, std::uint8_t format, std::uint32_t chunkStreamId)
{
    if (chunkStreamId < 64) {
        putU8(out, static_cast<std::uint8_t>((format << 6) | chunkStreamId));
    } else if (chunkStreamId < 320) {
        putU8(out, static_cast<std::uint8_t>(format << 6));
        putU8(out, static_cast<std::uint8_t>(chunkStreamId - 64));
    } else {
        putU8(out, static_cast<std::uint8_t>((format << 6) | 1));
        const std::uint32_t id = chunkStreamId - 64;
        putU8(out, static_cast<std::uint8_t>(id & 0xFF));
        putU8(out, static_cast<std::uint8_t>(id >> 8));
    }
}

} // namespace

void ChunkWriter::write(Bytes &out, std::uint32_t chunkStreamId, std::uint8_t type, std::uint32_t streamId,
                        std::uint32_t timestamp, ByteView payload) const
{
    const bool extended = timestamp >= kExtendedTimestamp;
    out.reserve(out.size() + payload.size() + 16 + payload.size() / chunkSize_ * 5);

    putBasicHeader(out, 0, chunkStreamId);
    putU24(out, extended ? kExtendedTimestamp : timestamp);
    putU24(out, static_cast<std::uint32_t>(payload.size()));
    putU8(out, type);
    putU32LE(out, streamId);
    if (extended)
        putU32(out, timestamp);

    size_t offset = 0;
    do {
        const size_t take = std::min<size_t>(chunkSize_, payload.size() - offset);
        putBytes(out, payload.subspan(offset, take));
        offset += take;
        if (offset < payload.size()) {
            putBasicHeader(out, 3, chunkStreamId);
            if (extended)
                putU32(out, timestamp); // repeated on continuation chunks, as librtmp and ffmpeg expect
        }
    } while (offset < payload.size());
}

bool ChunkReader::feed(ByteView data, std::vector<Message> &messages, std::string &error)
{
    putBytes(buffer_, data);
    while (!buffer_.empty()) {
        const long consumed = parseChunk(messages, error);
        if (consumed < 0)
            return false;
        if (consumed == 0)
            break;
        buffer_.erase(buffer_.begin(), buffer_.begin() + consumed);
    }
    return true;
}

long ChunkReader::parseChunk(std::vector<Message> &messages, std::string &error)
{
    const std::uint8_t *p = buffer_.data();
    const size_t available = buffer_.size();
    size_t pos = 0;

    const std::uint8_t format = p[0] >> 6;
    std::uint32_t chunkStreamId = p[0] & 0x3F;
    pos = 1;
    if (chunkStreamId == 0) {
        if (available < 2)
            return 0;
        chunkStreamId = 64 + p[1];
        pos = 2;
    } else if (chunkStreamId == 1) {
        if (available < 3)
            return 0;
        chunkStreamId = 64 + p[1] + (std::uint32_t{p[2]} << 8);
        pos = 3;
    }

    static constexpr size_t headerSizes[4] = {11, 7, 3, 0};
    if (available < pos + headerSizes[format])
        return 0;

    StreamState &state = streams_[chunkStreamId];
    if (format != 0 && !state.seen) {
        error = "RTMP server sent a compressed chunk header for an unknown chunk stream";
        return -1;
    }

    // Parse into locals first: if the chunk is incomplete, the stream state
    // must stay untouched until the rest arrives.
    std::uint32_t length = state.length;
    std::uint8_t type = state.type;
    std::uint32_t streamId = state.streamId;
    bool extended = state.extended;
    std::uint32_t rawTimestamp = 0;
    if (format <= 2) {
        rawTimestamp = readU24(p + pos);
        if (format <= 1) {
            length = readU24(p + pos + 3);
            type = p[pos + 6];
        }
        if (format == 0)
            streamId = readU32LE(p + pos + 7);
        extended = rawTimestamp == kExtendedTimestamp;
    }
    pos += headerSizes[format];

    if (extended) {
        if (available < pos + 4)
            return 0;
        if (format <= 2)
            rawTimestamp = readU32(p + pos);
        pos += 4; // type 3 chunks repeat the extended timestamp
    }

    if (length > kMaxMessageSize) {
        error = "RTMP server sent an oversized message";
        return -1;
    }
    if (!state.partial.empty() && format <= 1 && length != state.length) {
        error = "RTMP server changed a message's length mid-message";
        return -1;
    }

    const size_t take = std::min<size_t>(length - state.partial.size(), chunkSize_);
    if (available < pos + take)
        return 0;

    // The whole chunk is here: commit the header.
    const bool startsMessage = state.partial.empty();
    state.length = length;
    state.type = type;
    state.streamId = streamId;
    state.extended = extended;
    if (format == 0) {
        state.timestamp = rawTimestamp;
        state.delta = 0;
    } else if (format <= 2) {
        state.delta = rawTimestamp;
    }
    if (startsMessage && format != 0)
        state.timestamp += state.delta;
    state.seen = true;

    state.partial.insert(state.partial.end(), p + pos, p + pos + take);
    pos += take;

    if (state.partial.size() == state.length) {
        Message message{state.type, state.streamId, state.timestamp, std::move(state.partial)};
        state.partial.clear();
        if (message.type == SetChunkSize && message.payload.size() >= 4) {
            const std::uint32_t size = readU32(message.payload.data()) & 0x7FFFFFFF;
            if (size == 0 || size > kMaxMessageSize) {
                error = "RTMP server sent an invalid chunk size";
                return -1;
            }
            chunkSize_ = size; // applies to the very next chunk
        }
        messages.push_back(std::move(message));
    }
    return static_cast<long>(pos);
}

} // namespace abyss::net::rtmp
