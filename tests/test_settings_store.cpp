#include "TestHarness.hpp"
#include "app/SettingsStore.hpp"
#ifdef _WIN32
#include "platform/WindowsCredentialStore.hpp"
#endif

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <iostream>
#include <limits>

using namespace abyss;

namespace {

AppSettings sampleSettings()
{
    AppSettings settings;
    settings.video.canvasWidth = 2560;
    settings.video.canvasHeight = 1440;
    settings.video.outputWidth = 1280;
    settings.video.outputHeight = 720;
    settings.video.fps = 30;
    settings.video.videoBitrateKbps = 4500;
    settings.video.audioBitrateKbps = 128;
    settings.video.encoder = EncoderPreference::Software;
    settings.recording.directory = "D:/Recordings";
    settings.recording.format = RecordingFormat::Mp4;
    settings.desktopAudio = {"speakers-id", -12.5f, false, true};
    settings.microphone = {"", -3.0f, true, false};
    settings.windowGeometry = "AdnQywADAAA=";

    StreamDestination twitch;
    twitch.id = "twitch";
    twitch.platform = Platform::Twitch;
    twitch.name = "Twitch";
    twitch.server = "rtmp://live.twitch.tv/app";
    twitch.streamKey = "live_SUPER_SECRET_123";
    twitch.enabled = true;
    StreamDestination local;
    local.id = "c2b3";
    local.platform = Platform::Custom;
    local.name = "Local test";
    local.server = "rtmp://127.0.0.1:1935/live";
    local.streamKey = "test-key-456";
    local.enabled = false;
    settings.destinations = {twitch, local};
    return settings;
}

} // namespace

TEST_CASE("Settings round-trip through settings.json")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    SettingsStore store(dir.path());
    QString error;
    const bool saved = store.save(sampleSettings(), &error);
    if (!saved)
        std::cerr << error.toStdString() << std::endl;
    REQUIRE(saved);

    QString problem;
    const AppSettings loaded = store.load(&problem);
    CHECK(problem.isEmpty());
    CHECK(loaded.video.canvasWidth == 2560);
    CHECK(loaded.video.outputHeight == 720);
    CHECK(loaded.video.fps == 30);
    CHECK(loaded.video.videoBitrateKbps == 4500);
    CHECK(loaded.video.encoder == EncoderPreference::Software);
    CHECK(loaded.recording.directory == "D:/Recordings");
    CHECK(loaded.recording.format == RecordingFormat::Mp4);
    CHECK(loaded.desktopAudio.deviceId == "speakers-id");
    CHECK(loaded.desktopAudio.volumeDb == -12.5f);
    CHECK(loaded.microphone.deviceId.empty());
    CHECK(loaded.microphone.muted);
    CHECK(loaded.windowGeometry == "AdnQywADAAA=");
    REQUIRE(loaded.destinations.size() == 2);
    CHECK(loaded.destinations[0].platform == Platform::Twitch);
    CHECK(loaded.destinations[1].name == "Local test");
    CHECK(!loaded.destinations[1].enabled);
}

TEST_CASE("Stream keys never reach settings.json")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    SettingsStore store(dir.path());
    REQUIRE(store.save(sampleSettings()));

    QFile file(store.settingsPath());
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray text = file.readAll();
    CHECK(!text.contains("live_SUPER_SECRET_123"));
    CHECK(!text.contains("test-key-456"));
    CHECK(!text.contains("streamKey"));

    for (const auto &d : store.load().destinations)
        CHECK(d.streamKey.empty());
}

TEST_CASE("A missing settings file gives defaults with the starter destinations")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    SettingsStore store(dir.path());
    QString problem;
    const AppSettings loaded = store.load(&problem);
    CHECK(problem.isEmpty());
    CHECK(loaded.video.canvasWidth == 1920);
    CHECK(loaded.destinations.size() == 3);
    CHECK(!loaded.recording.directory.empty());
}

TEST_CASE("A corrupt settings file is set aside instead of crashing")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    SettingsStore store(dir.path());
    {
        QFile file(store.settingsPath());
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("{ this is not json");
    }
    QString problem;
    const AppSettings loaded = store.load(&problem);
    CHECK(!problem.isEmpty());
    CHECK(QFile::exists(store.settingsPath() + ".corrupt"));
    CHECK(loaded.destinations.size() == 3);
}

TEST_CASE("Invalid video values fall back to safe defaults")
{
    QJsonObject json = SettingsStore::toJson(sampleSettings());
    QJsonObject video = json["video"].toObject();
    video["fps"] = 1000;
    json["video"] = video;
    const AppSettings loaded = SettingsStore::fromJson(json);
    CHECK(loaded.video.fps == 60);
    CHECK(loaded.video.canvasWidth == 1920);
}

TEST_CASE("Silence is stored as a finite volume")
{
    AppSettings settings = sampleSettings();
    settings.desktopAudio.volumeDb = -std::numeric_limits<float>::infinity();
    const QByteArray json = QJsonDocument(SettingsStore::toJson(settings)).toJson();
    const AppSettings loaded = SettingsStore::fromJson(QJsonDocument::fromJson(json).object());
    CHECK(std::isfinite(loaded.desktopAudio.volumeDb));
    CHECK(loaded.desktopAudio.volumeDb <= -60.0f);
}

TEST_CASE("Duplicate destination ids in a hand-edited file are dropped")
{
    QJsonObject json = SettingsStore::toJson(sampleSettings());
    QJsonArray destinations = json["destinations"].toArray();
    destinations.append(destinations.first());
    json["destinations"] = destinations;
    CHECK(SettingsStore::fromJson(json).destinations.size() == 2);
}

TEST_CASE("Scene collections are stored next to the settings")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    SettingsStore store(dir.path());
    CHECK(!store.loadScenes().has_value());
    REQUIRE(store.saveScenes(R"({"version":1})"));
    CHECK(store.loadScenes().value_or("") == R"({"version":1})");
    REQUIRE(store.saveScenes(R"({"version":2})"));
    CHECK(QFile::exists(store.scenesPath() + ".bak"));
}

#ifdef _WIN32
// Touches the real Credential Manager, so it only runs when asked:
//   set ABYSS_TEST_CREDENTIAL_MANAGER=1
TEST_CASE("Windows Credential Manager round-trip (opt-in)")
{
    if (qEnvironmentVariableIntValue("ABYSS_TEST_CREDENTIAL_MANAGER") != 1)
        return;
    WindowsCredentialStore store;
    const std::string name =
        "AbyssStream/test/" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    std::string error;
    REQUIRE(store.write(name, "live_test_\xC3\xA9_key", error));
    CHECK(store.read(name).value_or("") == "live_test_\xC3\xA9_key");
    store.remove(name);
    CHECK(!store.read(name).has_value());
}
#endif
