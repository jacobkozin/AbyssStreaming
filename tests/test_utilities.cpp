#include "TestHarness.hpp"
#include "core/BitrateMeter.hpp"
#include "core/LogRedactor.hpp"
#include "core/MediaEngine.hpp"
#include "core/MockBackends.hpp"
#include "core/RecordingBackend.hpp"
#include "core/SecretStore.hpp"

#include <cmath>

using namespace abyss;

TEST_CASE("Log redactor masks every occurrence of a stream key")
{
    LogRedactor redactor;
    redactor.addSecret("live_8675309_secret");
    const auto line = redactor.redact("Connecting to rtmp://host/app/live_8675309_secret (live_8675309_secret)");
    CHECK(line.find("live_8675309_secret") == std::string::npos);
    CHECK(line == "Connecting to rtmp://host/app/******** (********)");

    redactor.removeSecret("live_8675309_secret");
    CHECK(redactor.redact("live_8675309_secret") == "live_8675309_secret");
}

TEST_CASE("Log redactor ignores very short strings and masks overlapping keys whole")
{
    LogRedactor redactor;
    redactor.addSecret("abc"); // too short to be a real key
    CHECK(redactor.redact("abcdef") == "abcdef");

    redactor.addSecret("key-1234");
    redactor.addSecret("key-1234-extended");
    CHECK(redactor.redact("x key-1234-extended y") == "x ******** y");
}

TEST_CASE("Bitrate meter converts byte counters to kbps")
{
    BitrateMeter meter;
    meter.addSample(0, 0);
    CHECK(meter.kbps() == 0.0);
    meter.addSample(1000, 750'000); // 6,000,000 bits in one second
    CHECK(std::abs(meter.kbps() - 6000.0) < 0.001);
    meter.addSample(2000, 1'500'000);
    CHECK(std::abs(meter.kbps() - 6000.0) < 0.001);

    meter.addSample(3000, 10); // counter reset: output was restarted
    CHECK(meter.kbps() == 0.0);
}

TEST_CASE("Recording file names are sortable and use the right extension")
{
    std::tm when{};
    when.tm_year = 2026 - 1900;
    when.tm_mon = 9;
    when.tm_mday = 20;
    when.tm_hour = 19;
    when.tm_min = 5;
    when.tm_sec = 9;
    CHECK(recordingFileName(when, RecordingFormat::Mkv) == "Abyss Stream 2026-10-20 19-05-09.mkv");
    CHECK(recordingFileName(when, RecordingFormat::Mp4) == "Abyss Stream 2026-10-20 19-05-09.mp4");
    CHECK(recordingFormatFromId("mp4") == RecordingFormat::Mp4);
    CHECK(recordingFormatFromId("anything-else") == RecordingFormat::Mkv);
}

TEST_CASE("Memory secret store keeps keys per destination")
{
    MemorySecretStore store;
    std::string error;
    CHECK(store.write(streamKeySecretName("twitch"), "live_abc", error));
    CHECK(store.read(streamKeySecretName("twitch")).value_or("") == "live_abc");
    CHECK(!store.read(streamKeySecretName("youtube")).has_value());
    store.remove(streamKeySecretName("twitch"));
    CHECK(!store.read(streamKeySecretName("twitch")).has_value());
    CHECK(!store.write("", "x", error));
}

TEST_CASE("Video settings validation catches impossible values")
{
    VideoSettings video;
    CHECK(validateVideoSettings(video).empty());

    auto odd = video;
    odd.outputWidth = 1279;
    CHECK(!validateVideoSettings(odd).empty());

    auto upscale = video;
    upscale.outputWidth = 2560;
    upscale.outputHeight = 1440;
    CHECK(!validateVideoSettings(upscale).empty());

    auto slow = video;
    slow.fps = 5;
    CHECK(!validateVideoSettings(slow).empty());

    CHECK(encoderPreferenceFromId(encoderPreferenceId(EncoderPreference::Software)) == EncoderPreference::Software);
    CHECK(encoderPreferenceFromId("bogus") == EncoderPreference::Auto);
}

TEST_CASE("Mock engine refuses invalid video settings")
{
    MockMediaEngine engine;
    VideoSettings video;
    video.fps = 30;
    std::string error;
    CHECK(engine.applyVideoSettings(video, error));
    CHECK(engine.videoSettings().fps == 30);

    video.videoBitrateKbps = 10;
    CHECK(!engine.applyVideoSettings(video, error));
    CHECK(!error.empty());
    CHECK(engine.videoSettings().videoBitrateKbps == 6000);
}

TEST_CASE("Mock audio backend applies device, volume and mute")
{
    MockAudioBackend audio;
    std::string error;
    CHECK(audio.setDevice(AudioChannel::Microphone, "mock-mic", error));
    CHECK(!audio.setDevice(AudioChannel::Microphone, "not-a-device", error));
    audio.setVolumeDb(AudioChannel::Microphone, -6.0f);
    audio.setMuted(AudioChannel::Microphone, true);

    const auto mic = audio.state(AudioChannel::Microphone);
    CHECK(mic.deviceId == "mock-mic");
    CHECK(mic.volumeDb == -6.0f);
    CHECK(mic.muted);
    CHECK(std::isinf(audio.level(AudioChannel::Microphone).leftDb)); // muted means silent

    CHECK(audio.setDevice(AudioChannel::Desktop, "", error)); // channel off
    CHECK(!audio.state(AudioChannel::Desktop).active);
}
