#include "net/RtmpSession.hpp"

#include <chrono>
#include <random>

namespace abyss::net {

namespace {

constexpr size_t kHandshakeSize = 1536;
constexpr std::uint32_t kOutgoingChunkSize = 4096;
constexpr std::uint32_t kWindowAckSize = 2'500'000;

using Clock = std::chrono::steady_clock;

int remainingMs(Clock::time_point deadline)
{
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left > 0 ? static_cast<int>(left) : 0;
}

std::string commandName(const std::vector<Amf0Value> &values)
{
    return values.empty() ? std::string() : values[0].string().value_or("");
}

double transactionId(const std::vector<Amf0Value> &values)
{
    return values.size() > 1 ? values[1].number().value_or(-1) : -1;
}

// The info object of an onStatus/_error reply, e.g. {code: "...", description: "..."}.
const Amf0Value *infoObject(const std::vector<Amf0Value> &values)
{
    for (size_t i = 2; i < values.size(); ++i) {
        if (values[i].object())
            return &values[i];
    }
    return nullptr;
}

std::string describeStatus(const Amf0Value *info)
{
    if (!info)
        return "no reason given";
    const std::string code = info->stringField("code");
    const std::string description = info->stringField("description");
    if (code.empty())
        return description.empty() ? "no reason given" : description;
    return description.empty() ? code : code + ": " + description;
}

} // namespace

RtmpSession::RtmpSession(ITransport &transport) : transport_(transport) {}

bool RtmpSession::fail(RtmpFailure kind, std::string &error, std::string text)
{
    failure_ = kind;
    error = std::move(text);
    return false;
}

bool RtmpSession::open(const PublishTarget &target, int timeoutMs, std::string &error)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    streamKey_ = target.streamKey;

    if (!transport_.connect(target.url.host, target.url.port, timeoutMs, error))
        return fail(RtmpFailure::Connect, error, error);
    if (!handshake(remainingMs(deadline), error))
        return false;

    // Larger chunks mean fewer chunk headers for video frames.
    Bytes chunkSize;
    putU32(chunkSize, kOutgoingChunkSize);
    if (!send(rtmp::ControlStream, rtmp::SetChunkSize, 0, 0, view(chunkSize), error))
        return false;
    writer_.setChunkSize(kOutgoingChunkSize);

    const double connectId = nextTransaction_++;
    if (!sendCommand(0, {"connect", connectId,
                         Amf0Object{{"app", target.url.app},
                                    {"type", "nonprivate"},
                                    {"flashVer", "FMLE/3.0 (compatible; FMSc/1.0)"},
                                    {"swfUrl", target.url.tcUrl},
                                    {"tcUrl", target.url.tcUrl}}},
                     error))
        return false;

    bool connected = false;
    std::string rejection;
    if (!waitFor(
            [&](const rtmp::Message &, const std::vector<Amf0Value> &values) {
                if (transactionId(values) != connectId)
                    return false;
                const std::string name = commandName(values);
                connected = name == "_result";
                if (!connected)
                    rejection = describeStatus(infoObject(values));
                return true;
            },
            remainingMs(deadline), error))
        return failure_ == RtmpFailure::None ? fail(RtmpFailure::Protocol, error, "No reply to connect: " + error)
                                             : false;
    if (!connected)
        return fail(RtmpFailure::Rejected, error, "The server refused the connection (" + rejection + ")");

    // releaseStream/FCPublish are what Flash Media Live Encoder and OBS send;
    // several ingest servers expect them before createStream.
    if (!sendCommand(0, {"releaseStream", nextTransaction_++, nullptr, streamKey_}, error) ||
        !sendCommand(0, {"FCPublish", nextTransaction_++, nullptr, streamKey_}, error))
        return false;
    const double createId = nextTransaction_++;
    if (!sendCommand(0, {"createStream", createId, nullptr}, error))
        return false;

    bool created = false;
    if (!waitFor(
            [&](const rtmp::Message &, const std::vector<Amf0Value> &values) {
                if (transactionId(values) != createId)
                    return false;
                if (commandName(values) == "_result" && values.size() > 3 && values[3].number()) {
                    streamId_ = static_cast<std::uint32_t>(*values[3].number());
                    created = true;
                }
                return true;
            },
            remainingMs(deadline), error))
        return failure_ == RtmpFailure::None ? fail(RtmpFailure::Protocol, error, "No reply to createStream: " + error)
                                             : false;
    if (!created)
        return fail(RtmpFailure::Rejected, error, "The server refused to create a stream");

    if (!sendCommand(streamId_, {"publish", nextTransaction_++, nullptr, streamKey_, "live"}, error))
        return false;

    std::string status;
    bool started = false;
    if (!waitFor(
            [&](const rtmp::Message &, const std::vector<Amf0Value> &values) {
                if (commandName(values) != "onStatus")
                    return false;
                const Amf0Value *info = infoObject(values);
                const std::string code = info ? info->stringField("code") : std::string();
                started = code == "NetStream.Publish.Start";
                status = describeStatus(info);
                return true;
            },
            remainingMs(deadline), error))
        return failure_ == RtmpFailure::None ? fail(RtmpFailure::Protocol, error, "No reply to publish: " + error)
                                             : false;
    if (!started)
        return fail(RtmpFailure::Rejected, error, "The server rejected the stream (" + status + "). Check the stream key.");

    publishing_ = true;
    return true;
}

bool RtmpSession::handshake(int timeoutMs, std::string &error)
{
    // Simple handshake: C0 (version 3) + C1 (time, zero, 1528 random bytes),
    // then echo the server's S1 back as C2.
    Bytes hello;
    hello.reserve(1 + kHandshakeSize);
    putU8(hello, 3);
    putU32(hello, 0);
    putU32(hello, 0);
    std::mt19937 random(std::random_device{}());
    for (size_t i = 8; i < kHandshakeSize; ++i)
        hello.push_back(static_cast<std::uint8_t>(random()));
    if (!transport_.sendAll(view(hello), error))
        return fail(RtmpFailure::Connect, error, error);
    bytesSent_ += hello.size();

    Bytes reply;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (reply.size() < 1 + 2 * kHandshakeSize) {
        std::uint8_t buffer[4096];
        const int received = transport_.receive(buffer, sizeof(buffer), remainingMs(deadline), error);
        if (received <= 0)
            return fail(RtmpFailure::Connect, error,
                        received == 0 ? "The server did not answer the RTMP handshake" : "Handshake failed: " + error);
        reply.insert(reply.end(), buffer, buffer + received);
    }
    if (reply[0] != 3)
        return fail(RtmpFailure::Protocol, error, "The server does not speak RTMP version 3 (is this an RTMP URL?)");

    const ByteView s1(reply.data() + 1, kHandshakeSize);
    if (!transport_.sendAll(s1, error))
        return fail(RtmpFailure::Connect, error, error);
    bytesSent_ += s1.size();

    // Anything after S0+S1+S2 is already the server's first chunk.
    const size_t used = 1 + 2 * kHandshakeSize;
    if (reply.size() > used) {
        std::vector<rtmp::Message> early;
        if (!reader_.feed({reply.data() + used, reply.size() - used}, early, error))
            return fail(RtmpFailure::Protocol, error, error);
        for (const auto &message : early) {
            if (!handleIncoming(message, error))
                return false;
        }
    }
    return true;
}

bool RtmpSession::send(std::uint32_t chunkStream, std::uint8_t type, std::uint32_t streamId, std::uint32_t timestamp,
                       ByteView payload, std::string &error)
{
    Bytes out;
    writer_.write(out, chunkStream, type, streamId, timestamp, payload);
    if (!transport_.sendAll(view(out), error))
        return fail(publishing_ ? RtmpFailure::Disconnected : RtmpFailure::Connect, error, error);
    bytesSent_ += out.size();
    return true;
}

bool RtmpSession::sendCommand(std::uint32_t streamId, const std::vector<Amf0Value> &values, std::string &error)
{
    Bytes payload;
    for (const auto &value : values)
        amf0Write(payload, value);
    return send(rtmp::CommandStream, rtmp::CommandAmf0, streamId, 0, view(payload), error);
}

bool RtmpSession::sendMetadata(const StreamMetadata &metadata, std::string &error)
{
    const Bytes payload = flvMetadata(metadata);
    return send(rtmp::DataStream, rtmp::DataAmf0, streamId_, 0, view(payload), error);
}

bool RtmpSession::sendVideo(std::uint32_t timestampMs, ByteView flvBody, std::string &error)
{
    return send(rtmp::VideoStream, rtmp::Video, streamId_, timestampMs, flvBody, error);
}

bool RtmpSession::sendAudio(std::uint32_t timestampMs, ByteView flvBody, std::string &error)
{
    return send(rtmp::AudioStream, rtmp::Audio, streamId_, timestampMs, flvBody, error);
}

bool RtmpSession::readAvailable(int timeoutMs, std::vector<rtmp::Message> &messages, std::string &error)
{
    std::uint8_t buffer[8192];
    const int received = transport_.receive(buffer, sizeof(buffer), timeoutMs, error);
    if (received < 0)
        return fail(publishing_ ? RtmpFailure::Disconnected : RtmpFailure::Connect, error, error);
    if (received == 0)
        return true;
    bytesReceived_ += static_cast<std::uint64_t>(received);
    if (!reader_.feed({buffer, static_cast<size_t>(received)}, messages, error))
        return fail(RtmpFailure::Protocol, error, error);

    // The receiver acknowledges every window's worth of bytes.
    if (ackWindow_ > 0 && bytesReceived_ - lastAcknowledged_ >= ackWindow_) {
        lastAcknowledged_ = bytesReceived_;
        Bytes ack;
        putU32(ack, static_cast<std::uint32_t>(bytesReceived_));
        if (!send(rtmp::ControlStream, rtmp::Acknowledgement, 0, 0, view(ack), error))
            return false;
    }
    return true;
}

bool RtmpSession::handleIncoming(const rtmp::Message &message, std::string &error)
{
    const auto &p = message.payload;
    switch (message.type) {
    case rtmp::WindowAckSize:
        if (p.size() >= 4)
            ackWindow_ = readU32(p.data());
        return true;
    case rtmp::SetPeerBandwidth: {
        // Answer with our own window size, as the spec asks.
        Bytes window;
        putU32(window, kWindowAckSize);
        return send(rtmp::ControlStream, rtmp::WindowAckSize, 0, 0, view(window), error);
    }
    case rtmp::UserControl:
        if (p.size() >= 6 && readU16(p.data()) == 6) { // ping request -> ping response
            Bytes pong;
            putU16(pong, 7);
            putBytes(pong, {p.data() + 2, 4});
            return send(rtmp::ControlStream, rtmp::UserControl, 0, 0, view(pong), error);
        }
        return true;
    case rtmp::CommandAmf0: {
        const auto values = amf0ReadAll(view(p));
        if (!values)
            return true; // ignore commands we cannot decode
        const std::string name = commandName(*values);
        if (publishing_ && (name == "onStatus" || name == "_error")) {
            const Amf0Value *info = infoObject(*values);
            const std::string level = info ? info->stringField("level") : std::string();
            if (level == "error" || name == "_error")
                return fail(RtmpFailure::Rejected, error, "The server stopped the stream (" + describeStatus(info) + ")");
        }
        return true;
    }
    default: return true;
    }
}

bool RtmpSession::waitFor(const std::function<bool(const rtmp::Message &, const std::vector<Amf0Value> &)> &done,
                          int timeoutMs, std::string &error)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        std::vector<rtmp::Message> messages;
        const int wait = remainingMs(deadline);
        if (wait == 0) {
            error = "timed out";
            return false;
        }
        if (!readAvailable(wait, messages, error))
            return false;
        for (const auto &message : messages) {
            if (!handleIncoming(message, error))
                return false;
            if (message.type != rtmp::CommandAmf0)
                continue;
            if (const auto values = amf0ReadAll(view(message.payload)); values && done(message, *values))
                return true;
        }
    }
}

bool RtmpSession::service(std::string &error)
{
    std::vector<rtmp::Message> messages;
    if (!readAvailable(0, messages, error))
        return false;
    for (const auto &message : messages) {
        if (!handleIncoming(message, error))
            return false;
    }
    return true;
}

void RtmpSession::close()
{
    if (publishing_) {
        std::string ignored;
        sendCommand(0, {"FCUnpublish", nextTransaction_++, nullptr, streamKey_}, ignored);
        sendCommand(0, {"deleteStream", nextTransaction_++, nullptr, static_cast<double>(streamId_)}, ignored);
        publishing_ = false;
    }
    transport_.close();
}

} // namespace abyss::net
