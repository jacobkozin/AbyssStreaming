#pragma once

#include "net/Amf0.hpp"
#include "net/Flv.hpp"
#include "net/RtmpChunk.hpp"
#include "net/RtmpUrl.hpp"
#include "net/Transport.hpp"
#include <functional>
#include <string>
#include <vector>

namespace abyss::net {

struct PublishTarget {
    RtmpUrl url;
    std::string streamKey; // sent as the publish stream name
};

// Why a session failed, in the categories the UI explains differently.
enum class RtmpFailure {
    None,
    Connect,      // could not reach the server / TLS failed
    Rejected,     // server refused the app, key or stream
    Disconnected, // connection dropped after publishing started
    Protocol,     // server spoke something we could not understand
};

// One RTMP publishing connection, driven synchronously by its owner thread:
// open() (handshake, connect, createStream, publish), then send media, then
// close(). It knows nothing about encoders or libobs.
class RtmpSession {
public:
    explicit RtmpSession(ITransport &transport);

    bool open(const PublishTarget &target, int timeoutMs, std::string &error);
    bool sendMetadata(const StreamMetadata &metadata, std::string &error);
    bool sendVideo(std::uint32_t timestampMs, ByteView flvBody, std::string &error);
    bool sendAudio(std::uint32_t timestampMs, ByteView flvBody, std::string &error);
    // Answers pings and notices server-side errors without blocking. Call it
    // regularly while streaming.
    bool service(std::string &error);
    // Unpublishes politely (FCUnpublish, deleteStream) and closes.
    void close();

    [[nodiscard]] std::uint64_t bytesSent() const noexcept { return bytesSent_; }
    [[nodiscard]] RtmpFailure failure() const noexcept { return failure_; }

private:
    bool handshake(int timeoutMs, std::string &error);
    bool send(std::uint32_t chunkStream, std::uint8_t type, std::uint32_t streamId, std::uint32_t timestamp,
              ByteView payload, std::string &error);
    bool sendCommand(std::uint32_t streamId, const std::vector<Amf0Value> &values, std::string &error);
    // Reads messages until `done` returns true or the timeout passes.
    bool waitFor(const std::function<bool(const rtmp::Message &, const std::vector<Amf0Value> &)> &done,
                 int timeoutMs, std::string &error);
    // Protocol control and housekeeping common to every received message.
    bool handleIncoming(const rtmp::Message &message, std::string &error);
    bool readAvailable(int timeoutMs, std::vector<rtmp::Message> &messages, std::string &error);
    bool fail(RtmpFailure kind, std::string &error, std::string text);

    ITransport &transport_;
    rtmp::ChunkWriter writer_;
    rtmp::ChunkReader reader_;
    std::string streamKey_;
    std::uint32_t streamId_{0};
    double nextTransaction_{1};
    std::uint64_t bytesSent_{0};
    std::uint64_t bytesReceived_{0};
    std::uint64_t lastAcknowledged_{0};
    std::uint32_t ackWindow_{0};
    bool publishing_{false};
    RtmpFailure failure_{RtmpFailure::None};
};

} // namespace abyss::net
