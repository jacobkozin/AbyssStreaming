#pragma once

#include "net/H264.hpp"
#include "net/RtmpSession.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace abyss::net {

struct EncodedPacket {
    enum class Kind { Video, Audio };
    Kind kind{Kind::Video};
    std::int64_t dtsUsec{0};
    std::int64_t ptsUsec{0};
    bool keyframe{false};
    Bytes data; // H.264 Annex B for video, raw AAC frames for audio
};

// The codec headers sent once, just before the first keyframe. Hardware
// encoders often only know their SPS/PPS after encoding a frame, so they can
// also be supplied later with setVideoParameterSets/setAudioSpecificConfig.
struct PublisherHeaders {
    AvcParameterSets parameterSets; // SPS/PPS from the video encoder
    Bytes audioSpecificConfig;      // from the AAC encoder
    StreamMetadata metadata;
};

struct PublisherCallbacks {
    std::function<void()> connected;                         // publishing started
    std::function<void(RtmpFailure, const std::string &)> failed; // connect failed or connection lost
    std::function<void()> stopped;                           // graceful stop finished
};

// One RTMP connection on its own thread. Encoders push packets from their
// threads; the publisher sends them in order and never blocks the caller.
// Reconnecting is the owner's job (libobs does it for the libobs output).
class RtmpPublisher {
public:
    using TransportFactory = std::function<std::unique_ptr<ITransport>()>;

    // `transportFactory` replaces the real TCP/TLS connection (tests).
    RtmpPublisher(PublishTarget target, PublisherHeaders headers, PublisherCallbacks callbacks, TlsOptions tls = {},
                  TransportFactory transportFactory = {});
    ~RtmpPublisher();
    RtmpPublisher(const RtmpPublisher &) = delete;
    RtmpPublisher &operator=(const RtmpPublisher &) = delete;

    void start();
    void push(EncodedPacket packet);
    void setVideoParameterSets(AvcParameterSets sets);
    void setAudioSpecificConfig(Bytes config);
    // Asks the thread to send what is queued, unpublish and close.
    void stop();
    void join();
    // No more callbacks from now on, e.g. while the owner is being destroyed.
    void silence() noexcept { silenced_ = true; }

    [[nodiscard]] std::uint64_t bytesSent() const noexcept { return bytesSent_; }
    [[nodiscard]] int droppedFrames() const noexcept { return droppedFrames_; }
    [[nodiscard]] int totalFrames() const noexcept { return totalFrames_; }

    // How far behind (in media time) the queue may fall before video frames
    // are dropped up to the next keyframe.
    static constexpr std::int64_t kMaxQueueUsec = 3'000'000;

private:
    void run();
    bool sendHeaders(RtmpSession &session, const PublisherHeaders &headers, std::string &error);
    bool sendPacket(RtmpSession &session, const EncodedPacket &packet, std::string &error);
    void dropForCongestion(); // caller holds mutex_

    PublishTarget target_;
    PublisherHeaders headers_;
    PublisherCallbacks callbacks_;
    TlsOptions tls_;
    TransportFactory transportFactory_;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<EncodedPacket> queue_;
    bool stopRequested_{false};

    // Owned by the publisher thread.
    bool started_{false};         // headers and first keyframe sent
    bool audioHeaderSent_{false}; // AAC config sent (may come after video starts)
    std::int64_t baseUsec_{0};

    std::atomic<std::uint64_t> bytesSent_{0};
    std::atomic<int> droppedFrames_{0};
    std::atomic<int> totalFrames_{0};
    std::atomic<bool> silenced_{false};

    void notifyConnected();
    void notifyFailed(RtmpFailure failure, const std::string &message);
    void notifyStopped();
};

} // namespace abyss::net
