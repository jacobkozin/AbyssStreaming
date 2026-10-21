#pragma once
#ifdef ABYSS_WITH_OBS
#include "core/RecordingBackend.hpp"
#include <obs.h>
#include <chrono>
#include <mutex>
#include <string>

namespace abyss {

class ObsEngine;

// Local recording through libobs' ffmpeg_muxer output. It reuses the shared
// stream encoders, so recording while live costs no extra encode.
class ObsRecordingBackend final : public IRecordingBackend {
public:
    explicit ObsRecordingBackend(ObsEngine &engine);
    ~ObsRecordingBackend() override;

    StartResult start(const RecordingOptions &options) override;
    void stop() override;
    RecordingStatus status() const override;

    void release();

private:
    static void onStart(void *data, calldata_t *params);
    static void onStop(void *data, calldata_t *params);

    ObsEngine &engine_;
    obs_output_t *output_{};

    mutable std::mutex mutex_;
    bool recording_{false};
    std::string path_;
    std::string message_;
    std::chrono::steady_clock::time_point startedAt_{};
};

} // namespace abyss
#endif
