// Headless end-to-end check of the real libobs pipeline: one shared H.264/AAC
// encoder pair feeding two RTMP outputs and a local recording at once.
//
// tools/run_e2e_test.ps1 starts two local RTMP test servers, runs this
// program against them, and then checks what the servers received.

#include "obs/ObsAudioBackend.hpp"
#include "obs/ObsEngine.hpp"
#include "obs/ObsRecordingBackend.hpp"
#include "obs/ObsSceneBackend.hpp"
#include "obs/ObsStreamBackend.hpp"
#include "core/PathUtf8.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <thread>

using namespace abyss;
using namespace std::chrono_literals;

namespace {

struct Options {
    std::string serverA, keyA, serverB, keyB, recordDir, logPath;
    int seconds{15};
    bool expectReconnectB{false};
    bool requireHardwareEncoder{false};
    bool abyssRtmp{false}; // use Abyss's own RTMP client instead of OBS's rtmp_output
};

const char *stateName(OutputState state)
{
    switch (state) {
    case OutputState::Offline: return "offline";
    case OutputState::Connecting: return "connecting";
    case OutputState::Live: return "live";
    case OutputState::Reconnecting: return "reconnecting";
    case OutputState::Stopping: return "stopping";
    case OutputState::Failed: return "failed";
    }
    return "?";
}

bool parse(int argc, char **argv, Options &options)
{
    std::map<std::string, std::string *> strings{
        {"--server-a", &options.serverA}, {"--key-a", &options.keyA},           {"--server-b", &options.serverB},
        {"--key-b", &options.keyB},       {"--record-dir", &options.recordDir}, {"--log", &options.logPath},
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (auto it = strings.find(arg); it != strings.end() && i + 1 < argc)
            *it->second = argv[++i];
        else if (arg == "--seconds" && i + 1 < argc)
            options.seconds = std::atoi(argv[++i]);
        else if (arg == "--expect-reconnect-b")
            options.expectReconnectB = true;
        else if (arg == "--require-hardware-encoder")
            options.requireHardwareEncoder = true;
        else if (arg == "--abyss-rtmp")
            options.abyssRtmp = true;
        else
            return false;
    }
    return !options.serverA.empty() && !options.serverB.empty() && !options.recordDir.empty();
}

std::string environment(const char *name)
{
#ifdef _WIN32
    char *value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || !value)
        return {};
    std::string result(value);
    std::free(value);
    return result;
#else
    const char *value = std::getenv(name);
    return value ? value : "";
#endif
}

ObsEngineConfig engineConfig(const std::filesystem::path &runtime, const std::string &logPath)
{
    auto native = [](std::filesystem::path p) { return pathToUtf8(p.make_preferred()); };
    ObsEngineConfig config;
    config.moduleConfigPath = native(std::filesystem::temp_directory_path() / "abyss-e2e-module-config");
    std::filesystem::create_directories(config.moduleConfigPath);
    config.moduleBinaryPath = native(runtime / "obs-plugins/64bit");
    config.moduleDataPath = native(runtime / "data/obs-plugins/%module%");
    config.libobsDataPath = native(runtime / "data/libobs");
    config.runtimeBinPath = native(runtime / "bin/64bit");
    config.graphicsModulePath = native(runtime / "bin/64bit/libobs-d3d11.dll");
    config.logPath = logPath;
    return config;
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    if (!parse(argc, argv, options)) {
        std::fprintf(stderr, "usage: abyss_stream_e2e --server-a URL --key-a KEY --server-b URL --key-b KEY "
                             "--record-dir DIR [--seconds N] [--expect-reconnect-b] [--require-hardware-encoder] "
                             "[--abyss-rtmp] [--log FILE]\n");
        return 2;
    }
    const std::string runtime = environment("ABYSS_OBS_RUNTIME");
    if (runtime.empty()) {
        std::fprintf(stderr, "Set ABYSS_OBS_RUNTIME to the OBS runtime root (bin/, data/, obs-plugins/).\n");
        return 2;
    }
    if (options.logPath.empty())
        options.logPath = pathToUtf8(std::filesystem::temp_directory_path() / "abyss-e2e-obs.log");

    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        std::printf("%s %s\n", ok ? "[pass]" : "[FAIL]", what);
        failures += ok ? 0 : 1;
    };

    ObsEngine engine;
    const auto started = engine.start(engineConfig(pathFromUtf8(runtime), options.logPath));
    if (!started.ok) {
        std::printf("[FAIL] engine start: %s (log: %s)\n", started.error.c_str(), options.logPath.c_str());
        return 1;
    }
    std::printf("encoder: %s%s\n", engine.videoEncoderName().c_str(),
                engine.usingEncoderFallback() ? " (fallback)" : "");
    if (options.requireHardwareEncoder)
        check(!engine.usingEncoderFallback(), "hardware encoder in use");

    int exitCode = 0;
    {
        auto audio = std::make_unique<ObsAudioBackend>();
        audio->restore(AudioChannel::Desktop, {});

        ObsSceneBackend scenes(1920, 1080);
        std::string error;
        check(scenes.initializeDefaults(error), "default scenes created");
        SourceRequest color;
        color.kind = SourceKind::Color;
        color.name = "Background";
        color.color = 0xFF203A70;
        check(scenes.addSource(color).ok, "color source added");
        SourceRequest text;
        text.kind = SourceKind::Text;
        text.name = "Title";
        text.text = "Abyss Stream end-to-end test";
        check(scenes.addSource(text).ok, "text source added");

        ObsStreamBackend stream(engine);
        stream.setTransportOptions({options.abyssRtmp});
        std::printf("rtmp client: %s\n", options.abyssRtmp ? "Abyss (src/net)" : "OBS rtmp_output");
        ObsRecordingBackend recording(engine);

        StreamDestination a{"a", Platform::Custom, "Server A", options.serverA, options.keyA, true};
        StreamDestination b{"b", Platform::Custom, "Server B", options.serverB, options.keyB, true};
        check(stream.start(a).ok, "output A started");
        check(stream.start(b).ok, "output B started");
        const auto recordingStart = recording.start({options.recordDir, RecordingFormat::Mkv});
        check(recordingStart.ok, "recording started");

        bool liveA = false;
        bool liveB = false;
        bool bLiveAfterReconnect = false;
        int maxReconnects = 0;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(options.seconds);
        while (std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(500ms);
            const auto ta = stream.telemetry("a");
            const auto tb = stream.telemetry("b");
            liveA = liveA || ta.state == OutputState::Live;
            liveB = liveB || tb.state == OutputState::Live;
            maxReconnects = std::max(maxReconnects, tb.reconnectAttempts);
            if (maxReconnects > 0 && tb.state == OutputState::Live)
                bLiveAfterReconnect = true;
            const auto rec = recording.status();
            std::printf("A %-12s %8llu B %-12s %8llu (reconnects %d) | rec %s %llu\n", stateName(ta.state),
                        static_cast<unsigned long long>(ta.bytesSent), stateName(tb.state),
                        static_cast<unsigned long long>(tb.bytesSent), tb.reconnectAttempts,
                        rec.active ? "on" : "off", static_cast<unsigned long long>(rec.bytesWritten));
            std::fflush(stdout);
        }

        const auto finalA = stream.telemetry("a");
        const auto finalB = stream.telemetry("b");
        stream.stopAll();
        recording.stop();
        for (int i = 0; i < 100; ++i) {
            if (!stream.telemetry("a").active() && !stream.telemetry("b").active() && !recording.status().active)
                break;
            std::this_thread::sleep_for(100ms);
        }

        check(liveA, "output A went live");
        check(liveB, "output B went live");
        check(finalA.bytesSent > 200'000, "output A sent media");
        check(finalB.bytesSent > 100'000, "output B sent media");
        check(finalA.droppedFrames == 0, "output A dropped no frames");
        if (options.expectReconnectB) {
            check(maxReconnects >= 1, "output B noticed the dropped connection");
            check(bLiveAfterReconnect, "output B reconnected by itself");
        }

        const auto rec = recording.status();
        std::error_code ec;
        const auto size = std::filesystem::file_size(pathFromUtf8(rec.path), ec);
        std::printf("recording: %s (%llu bytes) %s\n", rec.path.c_str(), static_cast<unsigned long long>(ec ? 0 : size),
                    rec.message.c_str());
        check(!ec && size > 200'000, "recording file written");
        check(!rec.active, "recording finalized");

        stream.releaseAll();
        recording.release();
        scenes.releaseAll();
        audio->releaseAll();
    }
    obs_wait_for_destroy_queue();
    engine.shutdown();

    exitCode = failures == 0 ? 0 : 1;
    std::printf("E2E %s (%d failed check(s)); libobs log: %s\n", exitCode == 0 ? "PASSED" : "FAILED", failures,
                options.logPath.c_str());
    return exitCode;
}
