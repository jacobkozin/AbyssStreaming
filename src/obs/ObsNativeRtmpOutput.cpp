#ifdef ABYSS_WITH_OBS
#include "obs/ObsNativeRtmpOutput.hpp"
#include "net/H264.hpp"
#include "net/RtmpPublisher.hpp"

#include <obs.h>
#include <util/base.h>

#include <memory>
#include <mutex>

namespace abyss {

namespace {

struct NativeOutput {
    obs_output_t *output{};
    std::mutex mutex; // guards publisher replacement against packet delivery
    std::unique_ptr<net::RtmpPublisher> publisher;
};

int stopCodeFor(net::RtmpFailure failure)
{
    switch (failure) {
    case net::RtmpFailure::Rejected: return OBS_OUTPUT_INVALID_STREAM;
    case net::RtmpFailure::Disconnected: return OBS_OUTPUT_DISCONNECTED;
    case net::RtmpFailure::Protocol: return OBS_OUTPUT_ERROR;
    case net::RtmpFailure::Connect:
    case net::RtmpFailure::None: break;
    }
    return OBS_OUTPUT_CONNECT_FAILED;
}

std::unique_ptr<net::RtmpPublisher> takePublisher(NativeOutput *self)
{
    std::scoped_lock lock(self->mutex);
    return std::move(self->publisher);
}

const char *outputName(void *)
{
    return "Abyss RTMP Output";
}

void *createOutput(obs_data_t *, obs_output_t *output)
{
    auto *self = new NativeOutput;
    self->output = output;
    return self;
}

void destroyOutput(void *data)
{
    auto *self = static_cast<NativeOutput *>(data);
    if (auto publisher = takePublisher(self)) {
        publisher->silence(); // the output is going away; do not call back into it
        publisher->stop();
        publisher->join();
    }
    delete self;
}

bool startOutput(void *data)
{
    auto *self = static_cast<NativeOutput *>(data);
    // A reconnect calls start again; the previous connection's thread has
    // already reported its failure and is finishing.
    if (auto previous = takePublisher(self))
        previous->join();

    obs_service_t *service = obs_output_get_service(self->output);
    const char *server = service ? obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL) : nullptr;
    const char *key = service ? obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_STREAM_KEY) : nullptr;
    const auto url = server ? net::parseRtmpUrl(server) : std::nullopt;
    if (!url) {
        obs_output_set_last_error(self->output, "The server URL is not a valid rtmp:// or rtmps:// URL.");
        return false;
    }
    if (!obs_output_can_begin_data_capture(self->output, 0) || !obs_output_initialize_encoders(self->output, 0))
        return false;

    obs_encoder_t *videoEncoder = obs_output_get_video_encoder(self->output);
    obs_encoder_t *audioEncoder = obs_output_get_audio_encoder(self->output, 0);
    std::uint8_t *extra = nullptr;
    size_t extraSize = 0;
    net::PublisherHeaders headers;
    if (!videoEncoder || !obs_encoder_get_extra_data(videoEncoder, &extra, &extraSize)) {
        obs_output_set_last_error(self->output, "The video encoder did not provide its H.264 headers.");
        return false;
    }
    const auto sets = net::findParameterSets({extra, extraSize});
    if (!sets) {
        obs_output_set_last_error(self->output, "The video encoder's H.264 headers have no SPS/PPS.");
        return false;
    }
    headers.parameterSets = *sets;
    if (audioEncoder && obs_encoder_get_extra_data(audioEncoder, &extra, &extraSize))
        headers.audioSpecificConfig.assign(extra, extra + extraSize);

    if (video_t *video = obs_output_video(self->output)) {
        const video_output_info *info = video_output_get_info(video);
        headers.metadata.width = static_cast<int>(info->width);
        headers.metadata.height = static_cast<int>(info->height);
        headers.metadata.framerate = static_cast<double>(info->fps_num) / info->fps_den;
    }
    obs_data_t *videoSettings = obs_encoder_get_settings(videoEncoder);
    headers.metadata.videoKbps = static_cast<int>(obs_data_get_int(videoSettings, "bitrate"));
    obs_data_release(videoSettings);
    if (audioEncoder) {
        obs_data_t *audioSettings = obs_encoder_get_settings(audioEncoder);
        headers.metadata.audioKbps = static_cast<int>(obs_data_get_int(audioSettings, "bitrate"));
        obs_data_release(audioSettings);
        headers.metadata.audioSampleRate = static_cast<int>(obs_encoder_get_sample_rate(audioEncoder));
    }

    net::PublisherCallbacks callbacks;
    callbacks.connected = [self] {
        blog(LOG_INFO, "[abyss-rtmp] publishing started");
        obs_output_begin_data_capture(self->output, 0);
    };
    callbacks.failed = [self](net::RtmpFailure failure, const std::string &message) {
        blog(LOG_WARNING, "[abyss-rtmp] %s", message.c_str());
        obs_output_set_last_error(self->output, message.c_str());
        // libobs retries DISCONNECTED itself (reconnect settings) by calling start again.
        obs_output_signal_stop(self->output, stopCodeFor(failure));
    };
    callbacks.stopped = [self] { obs_output_end_data_capture(self->output); };

    blog(LOG_INFO, "[abyss-rtmp] connecting to %s://%s:%u/%s", url->secure ? "rtmps" : "rtmp", url->host.c_str(),
         static_cast<unsigned>(url->port), url->app.c_str());
    auto publisher = std::make_unique<net::RtmpPublisher>(net::PublishTarget{*url, key ? key : ""}, std::move(headers),
                                                          std::move(callbacks));
    publisher->start();
    std::scoped_lock lock(self->mutex);
    self->publisher = std::move(publisher);
    return true;
}

void stopOutput(void *data, std::uint64_t)
{
    auto *self = static_cast<NativeOutput *>(data);
    std::scoped_lock lock(self->mutex);
    if (self->publisher)
        self->publisher->stop(); // the publisher thread ends data capture when it is done
    else
        obs_output_end_data_capture(self->output);
}

void encodedPacket(void *data, encoder_packet *packet)
{
    auto *self = static_cast<NativeOutput *>(data);
    if (!packet) { // the encoder failed
        obs_output_signal_stop(self->output, OBS_OUTPUT_ENCODE_ERROR);
        return;
    }

    net::EncodedPacket converted;
    converted.kind = packet->type == OBS_ENCODER_VIDEO ? net::EncodedPacket::Kind::Video : net::EncodedPacket::Kind::Audio;
    converted.dtsUsec = packet->dts_usec;
    const std::int64_t ptsOffsetUsec =
        (packet->pts - packet->dts) * 1'000'000LL * packet->timebase_num / std::max(1, packet->timebase_den);
    converted.ptsUsec = packet->dts_usec + ptsOffsetUsec;
    converted.keyframe = packet->keyframe;
    converted.data.assign(packet->data, packet->data + packet->size);

    std::scoped_lock lock(self->mutex);
    if (self->publisher)
        self->publisher->push(std::move(converted));
}

std::uint64_t totalBytes(void *data)
{
    auto *self = static_cast<NativeOutput *>(data);
    std::scoped_lock lock(self->mutex);
    return self->publisher ? self->publisher->bytesSent() : 0;
}

int droppedFrames(void *data)
{
    auto *self = static_cast<NativeOutput *>(data);
    std::scoped_lock lock(self->mutex);
    return self->publisher ? self->publisher->droppedFrames() : 0;
}

} // namespace

void registerAbyssRtmpOutput()
{
    obs_output_info info{};
    info.id = kAbyssRtmpOutputId;
    info.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED | OBS_OUTPUT_SERVICE;
    info.encoded_video_codecs = "h264";
    info.encoded_audio_codecs = "aac";
    info.protocols = "RTMP;RTMPS";
    info.get_name = outputName;
    info.create = createOutput;
    info.destroy = destroyOutput;
    info.start = startOutput;
    info.stop = stopOutput;
    info.encoded_packet = encodedPacket;
    info.get_total_bytes = totalBytes;
    info.get_dropped_frames = droppedFrames;
    obs_register_output(&info);
}

} // namespace abyss
#endif
