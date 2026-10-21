#include "net/RtmpPublisher.hpp"
#include "net/H264.hpp"

#include <chrono>

namespace abyss::net {

namespace {

constexpr int kConnectTimeoutMs = 10'000;
constexpr auto kDrainTimeout = std::chrono::seconds(2);

std::uint32_t toMilliseconds(std::int64_t usec)
{
    return usec <= 0 ? 0u : static_cast<std::uint32_t>(usec / 1000);
}

} // namespace

RtmpPublisher::RtmpPublisher(PublishTarget target, PublisherHeaders headers, PublisherCallbacks callbacks,
                             TlsOptions tls, TransportFactory transportFactory)
    : target_(std::move(target)), headers_(std::move(headers)), callbacks_(std::move(callbacks)), tls_(tls),
      transportFactory_(std::move(transportFactory))
{
}

RtmpPublisher::~RtmpPublisher()
{
    stop();
    join();
}

void RtmpPublisher::start()
{
    thread_ = std::thread(&RtmpPublisher::run, this);
}

void RtmpPublisher::push(EncodedPacket packet)
{
    {
        std::scoped_lock lock(mutex_);
        if (stopRequested_)
            return;
        if (packet.kind == EncodedPacket::Kind::Video)
            ++totalFrames_;
        queue_.push_back(std::move(packet));
        dropForCongestion();
    }
    wake_.notify_one();
}

void RtmpPublisher::stop()
{
    {
        std::scoped_lock lock(mutex_);
        stopRequested_ = true;
    }
    wake_.notify_one();
}

void RtmpPublisher::join()
{
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
        thread_.join();
}

void RtmpPublisher::dropForCongestion()
{
    if (queue_.size() < 2 || queue_.back().dtsUsec - queue_.front().dtsUsec <= kMaxQueueUsec)
        return;
    // The network cannot keep up. Drop queued video up to the next keyframe,
    // so the stream skips ahead instead of falling further behind; audio is
    // small and keeps flowing.
    bool sawKeyframe = false;
    std::deque<EncodedPacket> kept;
    for (auto &packet : queue_) {
        if (packet.kind == EncodedPacket::Kind::Video) {
            if (!sawKeyframe && packet.keyframe && &packet != &queue_.front())
                sawKeyframe = true;
            if (!sawKeyframe) {
                ++droppedFrames_;
                continue;
            }
        }
        kept.push_back(std::move(packet));
    }
    queue_ = std::move(kept);
}

bool RtmpPublisher::sendHeaders(RtmpSession &session, std::string &error)
{
    if (!session.sendMetadata(headers_.metadata, error))
        return false;
    const Bytes avcConfig = buildAvcDecoderConfig(headers_.parameterSets);
    if (!session.sendVideo(0, view(flvAvcSequenceHeader(view(avcConfig))), error))
        return false;
    if (!headers_.audioSpecificConfig.empty() &&
        !session.sendAudio(0, view(flvAacSequenceHeader(view(headers_.audioSpecificConfig))), error))
        return false;
    return true;
}

bool RtmpPublisher::sendPacket(RtmpSession &session, const EncodedPacket &packet, std::string &error)
{
    if (packet.kind == EncodedPacket::Kind::Video) {
        // Viewers can only start decoding at a keyframe, so the stream does too.
        if (!started_) {
            if (!packet.keyframe)
                return true;
            started_ = true;
            baseUsec_ = packet.dtsUsec;
        }
        const Bytes avcc = annexBToAvcc(view(packet.data));
        const auto compositionMs = static_cast<std::int32_t>((packet.ptsUsec - packet.dtsUsec) / 1000);
        return session.sendVideo(toMilliseconds(packet.dtsUsec - baseUsec_),
                                 view(flvAvcFrame(view(avcc), packet.keyframe, compositionMs)), error);
    }
    if (!started_ || packet.dtsUsec < baseUsec_)
        return true; // audio from before the first keyframe
    return session.sendAudio(toMilliseconds(packet.dtsUsec - baseUsec_), view(flvAacFrame(view(packet.data))), error);
}

void RtmpPublisher::run()
{
    auto transport = transportFactory_ ? transportFactory_() : makeTransport(target_.url.secure, tls_);
    if (!transport) {
        notifyFailed(RtmpFailure::Connect, "rtmps:// is not supported on this platform");
        return;
    }

    RtmpSession session(*transport);
    std::string error;
    if (!session.open(target_, kConnectTimeoutMs, error) || !sendHeaders(session, error)) {
        session.close();
        notifyFailed(session.failure() == RtmpFailure::None ? RtmpFailure::Connect : session.failure(), error);
        return;
    }
    bytesSent_ = session.bytesSent();
    notifyConnected();

    std::chrono::steady_clock::time_point drainDeadline{};
    while (true) {
        std::deque<EncodedPacket> batch;
        bool stopping = false;
        {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(100), [&] { return stopRequested_ || !queue_.empty(); });
            batch.swap(queue_);
            stopping = stopRequested_;
        }
        if (stopping && drainDeadline == std::chrono::steady_clock::time_point{})
            drainDeadline = std::chrono::steady_clock::now() + kDrainTimeout;

        for (const auto &packet : batch) {
            if (!sendPacket(session, packet, error)) {
                session.close();
                notifyFailed(RtmpFailure::Disconnected, error);
                return;
            }
        }
        bytesSent_ = session.bytesSent();

        if (!session.service(error)) {
            session.close();
            notifyFailed(session.failure() == RtmpFailure::None ? RtmpFailure::Disconnected : session.failure(), error);
            return;
        }

        if (stopping) {
            std::scoped_lock lock(mutex_);
            if (queue_.empty() || std::chrono::steady_clock::now() >= drainDeadline)
                break;
        }
    }

    session.close();
    notifyStopped();
}

void RtmpPublisher::notifyConnected()
{
    if (!silenced_ && callbacks_.connected)
        callbacks_.connected();
}

void RtmpPublisher::notifyFailed(RtmpFailure failure, const std::string &message)
{
    if (!silenced_ && callbacks_.failed)
        callbacks_.failed(failure, message);
}

void RtmpPublisher::notifyStopped()
{
    if (!silenced_ && callbacks_.stopped)
        callbacks_.stopped();
}

} // namespace abyss::net
