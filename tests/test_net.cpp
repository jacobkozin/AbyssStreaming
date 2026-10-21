#include "TestHarness.hpp"
#include "net/Amf0.hpp"
#include "net/Flv.hpp"
#include "net/H264.hpp"
#include "net/RtmpChunk.hpp"
#include "net/RtmpPublisher.hpp"
#include "net/RtmpSession.hpp"
#include "net/RtmpUrl.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace abyss;
using namespace abyss::net;

namespace {

// High-profile SPS for 1920x1080 and 1280x720, produced independently by the
// Python test server's encoder (tools/rtmp_test_server.py synthetic_sps).
const Bytes kSps1080 = {0x67, 0x64, 0x00, 0x2A, 0xAC, 0xE8, 0x07, 0x80, 0x22, 0x7E, 0x54};
const Bytes kSps720 = {0x67, 0x64, 0x00, 0x2A, 0xAC, 0xE8, 0x05, 0x00, 0x5B, 0x90};
const Bytes kPps = {0x68, 0xEE, 0x3C, 0x80};

Bytes annexB(std::initializer_list<Bytes> nals, bool fourByteStartCodes = true)
{
    Bytes out;
    for (const auto &nal : nals) {
        if (fourByteStartCodes)
            out.push_back(0);
        out.insert(out.end(), {0, 0, 1});
        out.insert(out.end(), nal.begin(), nal.end());
    }
    return out;
}

// An RTMP server living in memory: it answers the handshake, connect,
// createStream and publish, and records the media it receives.
class FakeRtmpServer final : public ITransport {
public:
    std::string expectedKey = "good-key";
    bool answerCommands = true;
    std::vector<rtmp::Message> media;
    std::vector<std::string> commands;
    int pongs = 0;

    bool connect(const std::string &, std::uint16_t, int, std::string &) override { return true; }

    bool sendAll(ByteView data, std::string &error) override
    {
        std::scoped_lock lock(mutex_);
        if (closed_) {
            error = "closed";
            return false;
        }
        inbound_.insert(inbound_.end(), data.begin(), data.end());
        process();
        return true;
    }

    int receive(std::uint8_t *buffer, size_t size, int timeoutMs, std::string &error) override
    {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return !outbound_.empty() || closed_; });
        if (outbound_.empty()) {
            if (closed_) {
                error = "closed";
                return -1;
            }
            return 0;
        }
        const size_t take = std::min(size, outbound_.size());
        std::copy(outbound_.begin(), outbound_.begin() + static_cast<std::ptrdiff_t>(take), buffer);
        outbound_.erase(outbound_.begin(), outbound_.begin() + static_cast<std::ptrdiff_t>(take));
        return static_cast<int>(take);
    }

    void close() override
    {
        std::scoped_lock lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    void sendPing()
    {
        std::scoped_lock lock(mutex_);
        Bytes ping;
        putU16(ping, 6);
        putU32(ping, 1234);
        writer_.write(outbound_, 2, rtmp::UserControl, 0, 0, view(ping));
        cv_.notify_all();
    }

    std::vector<rtmp::Message> mediaSnapshot()
    {
        std::scoped_lock lock(mutex_);
        return media;
    }

private:
    void reply(std::uint32_t streamId, std::initializer_list<Amf0Value> values)
    {
        Bytes payload;
        for (const auto &value : values)
            amf0Write(payload, value);
        writer_.write(outbound_, 3, rtmp::CommandAmf0, streamId, 0, view(payload));
    }

    void process()
    {
        if (stage_ == 0 && inbound_.size() >= 1537) { // C0 + C1
            Bytes s0s1s2(1 + 1536 * 2, 0);
            s0s1s2[0] = 3;
            std::copy(inbound_.begin() + 1, inbound_.begin() + 1537, s0s1s2.begin() + 1 + 1536); // S2 = C1
            outbound_.insert(outbound_.end(), s0s1s2.begin(), s0s1s2.end());
            inbound_.erase(inbound_.begin(), inbound_.begin() + 1537);
            stage_ = 1;
        }
        if (stage_ == 1 && inbound_.size() >= 1536) { // C2
            inbound_.erase(inbound_.begin(), inbound_.begin() + 1536);
            stage_ = 2;
        }
        if (stage_ == 2 && !inbound_.empty()) {
            std::vector<rtmp::Message> messages;
            std::string error;
            reader_.feed(view(inbound_), messages, error);
            inbound_.clear();
            for (const auto &message : messages)
                handle(message);
        }
        cv_.notify_all();
    }

    void handle(const rtmp::Message &message)
    {
        if (message.type == rtmp::UserControl && message.payload.size() >= 2 && readU16(message.payload.data()) == 7)
            ++pongs;
        if (message.type == rtmp::Audio || message.type == rtmp::Video || message.type == rtmp::DataAmf0)
            media.push_back(message);
        if (message.type != rtmp::CommandAmf0)
            return;
        const auto values = amf0ReadAll(view(message.payload));
        if (!values || values->empty())
            return;
        const std::string name = (*values)[0].string().value_or("");
        const double txn = values->size() > 1 ? (*values)[1].number().value_or(0) : 0;
        commands.push_back(name);
        if (!answerCommands)
            return;
        if (name == "connect") {
            Bytes window;
            putU32(window, 2'500'000);
            writer_.write(outbound_, 2, rtmp::WindowAckSize, 0, 0, view(window));
            Bytes chunk;
            putU32(chunk, 4096);
            writer_.write(outbound_, 2, rtmp::SetChunkSize, 0, 0, view(chunk));
            writer_.setChunkSize(4096);
            reply(0, {"_result", txn, Amf0Object{{"fmsVer", "FMS/3,0,1,123"}},
                      Amf0Object{{"level", "status"}, {"code", "NetConnection.Connect.Success"}}});
        } else if (name == "createStream") {
            reply(0, {"_result", txn, nullptr, 1.0});
        } else if (name == "publish") {
            const std::string key = values->size() > 3 ? (*values)[3].string().value_or("") : "";
            const bool ok = key == expectedKey;
            reply(1, {"onStatus", 0.0, nullptr,
                      Amf0Object{{"level", ok ? "status" : "error"},
                                 {"code", ok ? "NetStream.Publish.Start" : "NetStream.Publish.BadName"},
                                 {"description", ok ? "Publishing" : "Bad stream name"}}});
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    Bytes inbound_;
    Bytes outbound_;
    int stage_{0};
    bool closed_{false};
    rtmp::ChunkReader reader_;
    rtmp::ChunkWriter writer_;
};

PublishTarget target(const std::string &key)
{
    return {*parseRtmpUrl("rtmp://127.0.0.1:1935/live"), key};
}

} // namespace

TEST_CASE("AMF0 values round-trip, including nested objects")
{
    Bytes encoded;
    amf0Write(encoded, "connect");
    amf0Write(encoded, 1.0);
    amf0Write(encoded, Amf0Object{{"app", "live"}, {"nested", Amf0Object{{"flag", true}, {"n", 42}}}});
    amf0Write(encoded, nullptr);
    amf0WriteEcmaArray(encoded, Amf0Object{{"width", 1920}});

    const auto values = amf0ReadAll(view(encoded));
    REQUIRE(values.has_value());
    REQUIRE(values->size() == 5);
    CHECK((*values)[0].string() == "connect");
    CHECK((*values)[1].number() == 1.0);
    CHECK((*values)[2].stringField("app") == "live");
    REQUIRE((*values)[2].object() != nullptr);
    const auto &nested = (*values)[2].object()->at("nested");
    REQUIRE(nested.object() != nullptr);
    CHECK(nested.object()->at("n").number() == 42.0);
    CHECK((*values)[3].isNull());
    REQUIRE((*values)[4].object() != nullptr);
    CHECK((*values)[4].object()->at("width").number() == 1920.0);
}

TEST_CASE("AMF0 rejects truncated input instead of crashing")
{
    Bytes encoded;
    amf0Write(encoded, Amf0Object{{"code", "NetStream.Publish.Start"}});
    for (size_t cut = 1; cut < encoded.size(); ++cut)
        CHECK(!amf0ReadAll({encoded.data(), cut}).has_value());
}

TEST_CASE("Annex B is split on 3- and 4-byte start codes")
{
    const Bytes idr = {0x65, 0x88, 0x00, 0x00, 0x03, 0x01, 0x42}; // contains an escaped 00 00 03
    const Bytes stream = annexB({kSps1080, kPps, idr});
    const auto units = splitAnnexB(view(stream));
    REQUIRE(units.size() == 3);
    CHECK(nalType(units[0]) == NalType::Sps);
    CHECK(nalType(units[1]) == NalType::Pps);
    CHECK(Bytes(units[2].begin(), units[2].end()) == idr);
    CHECK(containsIdr(view(stream)));
    CHECK(splitAnnexB(view(annexB({kSps720, kPps}, false))).size() == 2);
}

TEST_CASE("Annex B converts to length-prefixed AVCC without delimiters")
{
    const Bytes aud = {0x09, 0xF0};
    const Bytes slice = {0x41, 0x9A, 0x02};
    const Bytes avcc = annexBToAvcc(view(annexB({aud, slice})));
    CHECK((avcc == Bytes{0x00, 0x00, 0x00, 0x03, 0x41, 0x9A, 0x02}));
    CHECK(!containsIdr(view(annexB({slice}))));
}

TEST_CASE("SPS parsing reads the real resolution")
{
    const auto hd = parseSps(view(kSps1080));
    REQUIRE(hd.has_value());
    CHECK(hd->width == 1920);
    CHECK(hd->height == 1080);
    CHECK(hd->profileIdc == 100);
    CHECK(hd->levelIdc == 42);
    const auto sd = parseSps(view(kSps720));
    REQUIRE(sd.has_value());
    CHECK(sd->width == 1280);
    CHECK(sd->height == 720);
    CHECK(!parseSps(view(kPps)).has_value());
}

TEST_CASE("AVC decoder config record carries the SPS and PPS")
{
    const auto sets = findParameterSets(view(annexB({kSps1080, kPps})));
    REQUIRE(sets.has_value());
    const Bytes config = buildAvcDecoderConfig(*sets);
    CHECK(config[0] == 1);
    CHECK(config[1] == 0x64); // High profile
    CHECK(config[3] == 0x2A); // level 4.2
    CHECK(config[4] == 0xFF); // 4-byte NAL lengths
    CHECK(config[5] == 0xE1); // one SPS
    CHECK(readU16(&config[6]) == kSps1080.size());
    CHECK(config[8 + kSps1080.size()] == 1); // one PPS
    CHECK(!findParameterSets(view(annexB({kPps}))).has_value());
}

TEST_CASE("FLV tag bodies use the right codec bytes")
{
    const Bytes frame = flvAvcFrame(view(Bytes{1, 2, 3}), true, 33);
    CHECK((Bytes(frame.begin(), frame.begin() + 5) == Bytes{0x17, 0x01, 0x00, 0x00, 0x21}));
    CHECK(flvAvcFrame(view(Bytes{1}), false, 0)[0] == 0x27);
    CHECK(flvAvcSequenceHeader(view(Bytes{1}))[1] == 0x00);
    const Bytes audio = flvAacFrame(view(Bytes{9}));
    CHECK((audio == Bytes{0xAF, 0x01, 0x09}));
    CHECK(flvAacSequenceHeader(view(Bytes{0x11, 0x90}))[1] == 0x00);

    const auto metadata = amf0ReadAll(view(flvMetadata({1920, 1080, 60.0, 6000, 160})));
    REQUIRE(metadata.has_value());
    REQUIRE(metadata->size() == 3);
    CHECK((*metadata)[0].string() == "@setDataFrame");
    CHECK((*metadata)[2].object()->at("height").number() == 1080.0);
}

TEST_CASE("RTMP URLs are parsed like librtmp does")
{
    const auto twitch = parseRtmpUrl("rtmp://live.twitch.tv/app");
    REQUIRE(twitch.has_value());
    CHECK(!twitch->secure);
    CHECK(twitch->port == 1935);
    CHECK(twitch->app == "app");
    CHECK(twitch->tcUrl == "rtmp://live.twitch.tv:1935/app");

    const auto youtube = parseRtmpUrl("rtmps://a.rtmps.youtube.com:443/live2");
    REQUIRE(youtube.has_value());
    CHECK(youtube->secure);
    CHECK(youtube->host == "a.rtmps.youtube.com");
    CHECK(youtube->port == 443);
    CHECK(youtube->app == "live2");

    const auto kick = parseRtmpUrl("rtmps://abc.global-contribute.live-video.net:443/app/");
    REQUIRE(kick.has_value());
    CHECK(kick->app == "app");

    const auto custom = parseRtmpUrl("RTMP://127.0.0.1:19350/live/instance");
    REQUIRE(custom.has_value());
    CHECK(custom->port == 19350);
    CHECK(custom->app == "live/instance");

    CHECK(parseRtmpUrl("rtmps://secure.example").value().port == 443);
    CHECK(!parseRtmpUrl("http://example.com/live").has_value());
    CHECK(!parseRtmpUrl("rtmp://example.com:99999/app").has_value());
    CHECK(!parseRtmpUrl("rtmp://:1935/app").has_value());
}

TEST_CASE("Chunked messages survive any split, chunk size and extended timestamps")
{
    rtmp::ChunkWriter writer;
    Bytes wire;
    Bytes big(10'000);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<std::uint8_t>(i * 7);
    writer.write(wire, 3, rtmp::CommandAmf0, 0, 0, view(Bytes{1, 2, 3}));
    writer.write(wire, 6, rtmp::Video, 1, 40, view(big)); // 128-byte chunks
    Bytes newSize;
    putU32(newSize, 4096);
    writer.write(wire, 2, rtmp::SetChunkSize, 0, 0, view(newSize));
    writer.setChunkSize(4096);
    writer.write(wire, 6, rtmp::Video, 1, 0x01000000, view(big)); // extended timestamp
    writer.write(wire, 4, rtmp::Audio, 1, 0x01000010, view(Bytes{0xAF, 1}));

    for (const size_t step : {size_t{1}, size_t{7}, size_t{4096}, wire.size()}) {
        rtmp::ChunkReader reader;
        std::vector<rtmp::Message> messages;
        std::string error;
        for (size_t offset = 0; offset < wire.size(); offset += step)
            REQUIRE(reader.feed({wire.data() + offset, std::min(step, wire.size() - offset)}, messages, error));
        REQUIRE(messages.size() == 5);
        CHECK((messages[0].payload == Bytes{1, 2, 3}));
        CHECK(messages[1].payload == big);
        CHECK(messages[1].timestamp == 40);
        CHECK(messages[1].streamId == 1);
        CHECK(messages[3].payload == big);
        CHECK(messages[3].timestamp == 0x01000000);
        CHECK(messages[4].timestamp == 0x01000010);
        CHECK(reader.chunkSize() == 4096);
    }
}

TEST_CASE("Chunk reader rejects nonsense from a server")
{
    rtmp::ChunkReader reader;
    std::vector<rtmp::Message> messages;
    std::string error;
    CHECK(!reader.feed(view(Bytes{0xC3, 0x00}), messages, error)); // type 3 on an unknown stream
    CHECK(!error.empty());
}

TEST_CASE("RTMP session publishes and answers pings")
{
    FakeRtmpServer server;
    RtmpSession session(server);
    std::string error;
    REQUIRE(session.open(target("good-key"), 2000, error));
    CHECK((server.commands == std::vector<std::string>{"connect", "releaseStream", "FCPublish", "createStream", "publish"}));

    CHECK(session.sendMetadata({1920, 1080, 60.0, 6000, 160}, error));
    CHECK(session.sendVideo(0, view(flvAvcFrame(view(Bytes{0, 0, 0, 1, 0x65}), true, 0)), error));
    CHECK(session.sendAudio(21, view(flvAacFrame(view(Bytes{1, 2}))), error));
    server.sendPing();
    CHECK(session.service(error));

    const auto media = server.mediaSnapshot();
    REQUIRE(media.size() == 3);
    CHECK(media[0].type == rtmp::DataAmf0);
    CHECK(media[1].type == rtmp::Video);
    CHECK(media[1].streamId == 1);
    CHECK(media[2].timestamp == 21);
    CHECK(server.pongs == 1);
    CHECK(session.bytesSent() > 3000);

    session.close();
    CHECK(server.commands.back() == "deleteStream");
}

TEST_CASE("RTMP session explains a rejected stream key")
{
    FakeRtmpServer server;
    RtmpSession session(server);
    std::string error;
    CHECK(!session.open(target("wrong-key"), 2000, error));
    CHECK(session.failure() == RtmpFailure::Rejected);
    CHECK(error.find("BadName") != std::string::npos);
    CHECK(error.find("stream key") != std::string::npos);
}

TEST_CASE("RTMP session times out on a silent server")
{
    FakeRtmpServer server;
    server.answerCommands = false;
    RtmpSession session(server);
    std::string error;
    CHECK(!session.open(target("good-key"), 300, error));
    CHECK(session.failure() == RtmpFailure::Protocol);
    CHECK(error.find("connect") != std::string::npos);
}

TEST_CASE("Publisher starts on a keyframe and rebases timestamps")
{
    auto server = std::make_shared<FakeRtmpServer>();
    PublisherHeaders headers;
    headers.parameterSets = {kSps1080, kPps};
    headers.audioSpecificConfig = {0x11, 0x90};
    headers.metadata = {1920, 1080, 60.0, 6000, 160};

    std::mutex mutex;
    std::condition_variable cv;
    bool connected = false;
    bool stopped = false;
    PublisherCallbacks callbacks;
    callbacks.connected = [&] {
        std::scoped_lock lock(mutex);
        connected = true;
        cv.notify_all();
    };
    callbacks.stopped = [&] {
        std::scoped_lock lock(mutex);
        stopped = true;
        cv.notify_all();
    };
    callbacks.failed = [&](RtmpFailure, const std::string &) {
        std::scoped_lock lock(mutex);
        stopped = true;
        cv.notify_all();
    };

    // The fake server is shared with the test so it outlives the publisher.
    struct Forwarding final : ITransport {
        std::shared_ptr<FakeRtmpServer> server;
        bool connect(const std::string &h, std::uint16_t p, int t, std::string &e) override { return server->connect(h, p, t, e); }
        bool sendAll(ByteView d, std::string &e) override { return server->sendAll(d, e); }
        int receive(std::uint8_t *b, size_t s, int t, std::string &e) override { return server->receive(b, s, t, e); }
        void close() override { server->close(); }
    };
    RtmpPublisher publisher(target("good-key"), headers, callbacks, {}, [server] {
        auto transport = std::make_unique<Forwarding>();
        transport->server = server;
        return transport;
    });
    publisher.start();
    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return connected || stopped; }));
        REQUIRE(connected);
    }

    const Bytes inter = annexB({{0x41, 0x01}});
    const Bytes idr = annexB({{0x65, 0x01}});
    auto packet = [](EncodedPacket::Kind kind, std::int64_t dtsMs, bool key, const Bytes &data) {
        EncodedPacket p;
        p.kind = kind;
        p.dtsUsec = dtsMs * 1000;
        p.ptsUsec = p.dtsUsec + (kind == EncodedPacket::Kind::Video ? 33'000 : 0);
        p.keyframe = key;
        p.data = data;
        return p;
    };
    publisher.push(packet(EncodedPacket::Kind::Video, 1000, false, inter));      // before any keyframe: dropped
    publisher.push(packet(EncodedPacket::Kind::Audio, 1005, false, {0x21}));     // before the keyframe: dropped
    publisher.push(packet(EncodedPacket::Kind::Video, 1016, true, idr));         // stream starts here (t=0)
    publisher.push(packet(EncodedPacket::Kind::Audio, 1037, false, {0x22}));     // t=21
    publisher.push(packet(EncodedPacket::Kind::Video, 1033, false, inter));      // t=17
    publisher.stop();
    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return stopped; }));
    }
    publisher.join();

    std::vector<rtmp::Message> video;
    std::vector<rtmp::Message> audio;
    for (const auto &message : server->mediaSnapshot()) {
        if (message.type == rtmp::Video)
            video.push_back(message);
        if (message.type == rtmp::Audio)
            audio.push_back(message);
    }
    REQUIRE(video.size() == 3); // sequence header + keyframe + inter frame
    CHECK(video[0].payload[1] == 0x00);
    CHECK(video[1].payload[0] == 0x17);
    CHECK(video[1].timestamp == 0);
    CHECK(video[1].payload[4] == 33); // composition time offset
    CHECK(video[2].timestamp == 17);
    REQUIRE(audio.size() == 2); // sequence header + one frame
    CHECK(audio[1].timestamp == 21);
    CHECK(server->commands.back() == "deleteStream");
}

TEST_CASE("Publisher drops video up to the next keyframe when it falls behind")
{
    PublisherHeaders headers;
    headers.parameterSets = {kSps1080, kPps};
    RtmpPublisher publisher(target("good-key"), headers, {}); // never started: the queue just fills
    for (int i = 0; i < 300; ++i) {
        EncodedPacket p;
        p.kind = EncodedPacket::Kind::Video;
        p.dtsUsec = static_cast<std::int64_t>(i) * 16'667;
        p.keyframe = i % 120 == 0; // keyframe every 2 s at 60 FPS
        p.data = {0, 0, 1, static_cast<std::uint8_t>(p.keyframe ? 0x65 : 0x41)};
        publisher.push(std::move(p));
    }
    CHECK(publisher.totalFrames() == 300);
    CHECK(publisher.droppedFrames() > 0);
    CHECK(publisher.droppedFrames() < 300);
}

// Talks to a real RTMP server over TCP (or TLS for rtmps://), so it only runs
// when asked, e.g. against the Python test server:
//   python tools/rtmp_test_server.py --port 19360 --key live-test
//   set ABYSS_TEST_RTMP_URL=rtmp://127.0.0.1:19360/live
//   set ABYSS_TEST_RTMP_KEY=live-test
TEST_CASE("Publishes to a live RTMP server (opt-in: ABYSS_TEST_RTMP_URL)")
{
    const std::string server = abyss::test::environment("ABYSS_TEST_RTMP_URL");
    if (server.empty())
        return;
    const auto url = parseRtmpUrl(server);
    REQUIRE(url.has_value());
    TlsOptions tls;
    tls.allowUntrustedLocalhost = true; // only honoured for localhost test servers
    auto transport = makeTransport(url->secure, tls);
    REQUIRE(transport != nullptr);

    RtmpSession session(*transport);
    std::string error;
    const bool opened = session.open({*url, abyss::test::environment("ABYSS_TEST_RTMP_KEY")}, 5000, error);
    if (!opened)
        std::cerr << "open failed: " << error << '\n';
    REQUIRE(opened);
    CHECK(session.sendMetadata({1920, 1080, 30.0, 2500, 128}, error));
    const Bytes config = buildAvcDecoderConfig({kSps1080, kPps});
    CHECK(session.sendVideo(0, view(flvAvcSequenceHeader(view(config))), error));
    CHECK(session.sendAudio(0, view(flvAacSequenceHeader(view(Bytes{0x11, 0x90}))), error));
    for (std::uint32_t i = 0; i < 90; ++i) {
        const bool key = i % 30 == 0;
        const Bytes frame = annexBToAvcc(view(annexB({{static_cast<std::uint8_t>(key ? 0x65 : 0x41), 0x88, 0x84}})));
        CHECK(session.sendVideo(i * 33, view(flvAvcFrame(view(frame), key, 0)), error));
        CHECK(session.sendAudio(i * 33, view(flvAacFrame(view(Bytes(64, 0x21)))), error));
        CHECK(session.service(error));
    }
    session.close();
}

// Point this at an rtmps:// server with a self-signed certificate: without the
// localhost test exception, Schannel must refuse it.
TEST_CASE("Refuses an untrusted TLS certificate (opt-in: ABYSS_TEST_RTMPS_UNTRUSTED_URL)")
{
    const std::string server = abyss::test::environment("ABYSS_TEST_RTMPS_UNTRUSTED_URL");
    if (server.empty())
        return;
    const auto url = parseRtmpUrl(server);
    REQUIRE(url.has_value() && url->secure);
    auto transport = makeTransport(true); // default options: certificates are verified
    REQUIRE(transport != nullptr);
    RtmpSession session(*transport);
    std::string error;
    CHECK(!session.open({*url, "any-key"}, 5000, error));
    CHECK(session.failure() == RtmpFailure::Connect);
    CHECK(error.find("certificate") != std::string::npos);
    std::cout << "    refused as expected: " << error << '\n';
}

TEST_CASE("Publisher waits for codec headers that arrive after it connects")
{
    auto server = std::make_shared<FakeRtmpServer>();
    struct Forwarding final : ITransport {
        std::shared_ptr<FakeRtmpServer> server;
        bool connect(const std::string &h, std::uint16_t p, int t, std::string &e) override { return server->connect(h, p, t, e); }
        bool sendAll(ByteView d, std::string &e) override { return server->sendAll(d, e); }
        int receive(std::uint8_t *b, size_t s, int t, std::string &e) override { return server->receive(b, s, t, e); }
        void close() override { server->close(); }
    };
    std::mutex mutex;
    std::condition_variable cv;
    bool connected = false;
    bool finished = false;
    PublisherCallbacks callbacks;
    callbacks.connected = [&] {
        std::scoped_lock lock(mutex);
        connected = true;
        cv.notify_all();
    };
    callbacks.stopped = [&] {
        std::scoped_lock lock(mutex);
        finished = true;
        cv.notify_all();
    };

    // No SPS/PPS up front, like NVENC before its first frame.
    RtmpPublisher publisher(target("good-key"), {}, callbacks, {}, [server] {
        auto transport = std::make_unique<Forwarding>();
        transport->server = server;
        return transport;
    });
    publisher.start();
    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return connected; }));
    }

    // The encoder hands its headers over just before its first packet.
    publisher.setVideoParameterSets({kSps720, kPps});
    publisher.setAudioSpecificConfig({0x11, 0x90});
    EncodedPacket keyframe;
    keyframe.kind = EncodedPacket::Kind::Video;
    keyframe.keyframe = true;
    keyframe.dtsUsec = keyframe.ptsUsec = 2'000'000;
    keyframe.data = annexB({{0x65, 0x01}});
    publisher.push(keyframe);
    EncodedPacket inter = keyframe;
    inter.dtsUsec = inter.ptsUsec = 2'016'000;
    inter.keyframe = false;
    publisher.push(inter);
    publisher.stop();
    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return finished; }));
    }
    publisher.join();

    std::vector<rtmp::Message> video;
    for (const auto &message : server->mediaSnapshot()) {
        if (message.type == rtmp::Video)
            video.push_back(message);
    }
    REQUIRE(video.size() == 3); // sequence header, keyframe at t=0, inter frame at t=16
    CHECK(video[0].payload[1] == 0x00);
    const auto config = ByteView(video[0].payload).subspan(5);
    CHECK(readU16(config.data() + 6) == kSps720.size());
    CHECK(video[1].timestamp == 0);
    CHECK(video[2].timestamp == 16);
}

TEST_CASE("Publisher can take SPS/PPS from inside the first keyframe")
{
    auto server = std::make_shared<FakeRtmpServer>();
    struct Forwarding final : ITransport {
        std::shared_ptr<FakeRtmpServer> server;
        bool connect(const std::string &h, std::uint16_t p, int t, std::string &e) override { return server->connect(h, p, t, e); }
        bool sendAll(ByteView d, std::string &e) override { return server->sendAll(d, e); }
        int receive(std::uint8_t *b, size_t s, int t, std::string &e) override { return server->receive(b, s, t, e); }
        void close() override { server->close(); }
    };
    std::mutex mutex;
    std::condition_variable cv;
    bool connected = false;
    bool finished = false;
    PublisherCallbacks callbacks;
    callbacks.connected = [&] {
        std::scoped_lock lock(mutex);
        connected = true;
        cv.notify_all();
    };
    callbacks.stopped = [&] {
        std::scoped_lock lock(mutex);
        finished = true;
        cv.notify_all();
    };
    RtmpPublisher publisher(target("good-key"), {}, callbacks, {}, [server] {
        auto transport = std::make_unique<Forwarding>();
        transport->server = server;
        return transport;
    });
    publisher.start();
    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return connected; }));
    }
    EncodedPacket keyframe;
    keyframe.kind = EncodedPacket::Kind::Video;
    keyframe.keyframe = true;
    keyframe.data = annexB({kSps1080, kPps, {0x65, 0x01}});
    publisher.push(keyframe);
    publisher.stop();
    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return finished; }));
    }
    publisher.join();
    size_t videoMessages = 0;
    for (const auto &message : server->mediaSnapshot())
        videoMessages += message.type == rtmp::Video ? 1 : 0;
    CHECK(videoMessages == 2); // sequence header + keyframe
}

TEST_CASE("Publisher holds back a keyframe it has no headers for")
{
    auto server = std::make_shared<FakeRtmpServer>();
    struct Forwarding final : ITransport {
        std::shared_ptr<FakeRtmpServer> server;
        bool connect(const std::string &h, std::uint16_t p, int t, std::string &e) override { return server->connect(h, p, t, e); }
        bool sendAll(ByteView d, std::string &e) override { return server->sendAll(d, e); }
        int receive(std::uint8_t *b, size_t s, int t, std::string &e) override { return server->receive(b, s, t, e); }
        void close() override { server->close(); }
    };
    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;
    PublisherCallbacks callbacks;
    callbacks.stopped = [&] {
        std::scoped_lock lock(mutex);
        finished = true;
        cv.notify_all();
    };
    RtmpPublisher publisher(target("good-key"), {}, callbacks, {}, [server] {
        auto transport = std::make_unique<Forwarding>();
        transport->server = server;
        return transport;
    });
    publisher.start();
    EncodedPacket keyframe;
    keyframe.kind = EncodedPacket::Kind::Video;
    keyframe.keyframe = true;
    keyframe.data = annexB({{0x65, 0x01}}); // no SPS/PPS anywhere
    publisher.push(keyframe);
    publisher.stop();
    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return finished; }));
    }
    publisher.join();
    for (const auto &message : server->mediaSnapshot())
        CHECK(message.type != rtmp::Video); // nothing undecodable was sent
}
