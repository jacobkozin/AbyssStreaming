#include "app/SettingsStore.hpp"
#include "app/AppPaths.hpp"
#include "core/Platforms.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <algorithm>
#include <set>

namespace abyss {

namespace {

constexpr int kSettingsVersion = 1;
// JSON has no infinity, and the mixer's slider stops at -60 dB anyway.
constexpr double kQuietestStoredDb = -100.0;

QString q(const std::string &text) { return QString::fromStdString(text); }
std::string s(const QJsonValue &value) { return value.toString().toStdString(); }

QJsonObject channelToJson(const AudioChannelState &channel)
{
    return {
        {"device", q(channel.deviceId)},
        {"volumeDb", std::clamp(static_cast<double>(channel.volumeDb), kQuietestStoredDb, 0.0)},
        {"muted", channel.muted},
    };
}

AudioChannelState channelFromJson(const QJsonValue &value)
{
    AudioChannelState channel;
    if (!value.isObject())
        return channel;
    const QJsonObject json = value.toObject();
    if (json.contains("device"))
        channel.deviceId = s(json["device"]);
    channel.volumeDb = static_cast<float>(std::clamp(json["volumeDb"].toDouble(0.0), kQuietestStoredDb, 0.0));
    channel.muted = json["muted"].toBool(false);
    return channel;
}

} // namespace

SettingsStore::SettingsStore(QString directory) : directory_(std::move(directory)) {}

QString SettingsStore::settingsPath() const
{
    return QDir(directory_).filePath("settings.json");
}

QString SettingsStore::scenesPath() const
{
    return QDir(directory_).filePath("scenes.json");
}

QJsonObject SettingsStore::toJson(const AppSettings &settings)
{
    const VideoSettings &video = settings.video;
    QJsonObject videoJson{
        {"canvasWidth", static_cast<int>(video.canvasWidth)},
        {"canvasHeight", static_cast<int>(video.canvasHeight)},
        {"outputWidth", static_cast<int>(video.outputWidth)},
        {"outputHeight", static_cast<int>(video.outputHeight)},
        {"fps", static_cast<int>(video.fps)},
        {"videoBitrateKbps", video.videoBitrateKbps},
        {"audioBitrateKbps", video.audioBitrateKbps},
        {"encoder", q(std::string(encoderPreferenceId(video.encoder)))},
    };

    QJsonArray destinations;
    for (const auto &d : settings.destinations) {
        // No "streamKey" field on purpose: keys only go to the secret store.
        destinations.append(QJsonObject{
            {"id", q(d.id)},
            {"platform", q(std::string(presetFor(d.platform).id))},
            {"name", q(d.name)},
            {"server", q(d.server)},
            {"enabled", d.enabled},
        });
    }

    return {
        {"version", kSettingsVersion},
        {"video", videoJson},
        {"recording",
         QJsonObject{{"directory", q(settings.recording.directory)},
                     {"format", q(std::string(recordingFormatId(settings.recording.format)))}}},
        {"audio",
         QJsonObject{{"desktop", channelToJson(settings.desktopAudio)},
                     {"microphone", channelToJson(settings.microphone)}}},
        {"destinations", destinations},
        {"streaming", QJsonObject{{"abyssRtmpClient", settings.transport.abyssRtmpClient}}},
        {"window", QJsonObject{{"geometry", q(settings.windowGeometry)}}},
    };
}

AppSettings SettingsStore::fromJson(const QJsonObject &json)
{
    AppSettings settings;

    const QJsonObject videoJson = json["video"].toObject();
    VideoSettings video;
    video.canvasWidth = static_cast<std::uint32_t>(videoJson["canvasWidth"].toInt(static_cast<int>(video.canvasWidth)));
    video.canvasHeight = static_cast<std::uint32_t>(videoJson["canvasHeight"].toInt(static_cast<int>(video.canvasHeight)));
    video.outputWidth = static_cast<std::uint32_t>(videoJson["outputWidth"].toInt(static_cast<int>(video.outputWidth)));
    video.outputHeight = static_cast<std::uint32_t>(videoJson["outputHeight"].toInt(static_cast<int>(video.outputHeight)));
    video.fps = static_cast<std::uint32_t>(videoJson["fps"].toInt(static_cast<int>(video.fps)));
    video.videoBitrateKbps = videoJson["videoBitrateKbps"].toInt(video.videoBitrateKbps);
    video.audioBitrateKbps = videoJson["audioBitrateKbps"].toInt(video.audioBitrateKbps);
    video.encoder = encoderPreferenceFromId(s(videoJson["encoder"]));
    // A hand-edited or corrupted value must not stop the engine starting.
    settings.video = validateVideoSettings(video).empty() ? video : VideoSettings{};

    const QJsonObject recording = json["recording"].toObject();
    settings.recording.directory = s(recording["directory"]);
    settings.recording.format = recordingFormatFromId(s(recording["format"]));

    const QJsonObject audio = json["audio"].toObject();
    settings.desktopAudio = channelFromJson(audio["desktop"]);
    settings.microphone = channelFromJson(audio["microphone"]);

    if (json.contains("destinations")) {
        std::set<std::string> seen;
        for (const auto &value : json["destinations"].toArray()) {
            const QJsonObject d = value.toObject();
            StreamDestination destination;
            destination.id = s(d["id"]);
            if (destination.id.empty() || !seen.insert(destination.id).second)
                continue;
            destination.platform = platformFromId(s(d["platform"]));
            destination.name = s(d["name"]);
            destination.server = s(d["server"]);
            destination.enabled = d["enabled"].toBool(true);
            settings.destinations.push_back(std::move(destination));
        }
    } else {
        settings.destinations = defaultDestinations();
    }

    settings.transport.abyssRtmpClient = json["streaming"].toObject()["abyssRtmpClient"].toBool(false);
    settings.windowGeometry = s(json["window"].toObject()["geometry"]);
    return settings;
}

AppSettings SettingsStore::load(QString *problem) const
{
    AppSettings settings;
    QFile file(settingsPath());
    if (file.exists()) {
        QJsonParseError parseError{};
        QByteArray bytes;
        if (file.open(QIODevice::ReadOnly))
            bytes = file.readAll();
        file.close();
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            settings = fromJson(document.object());
        } else {
            const QString keep = settingsPath() + ".corrupt";
            QFile::remove(keep);
            QFile::rename(settingsPath(), keep);
            if (problem)
                *problem = "Settings could not be read (" + parseError.errorString() +
                           "). Defaults were loaded and the old file was kept as " + keep + ".";
            settings.destinations = defaultDestinations();
        }
    } else {
        settings.destinations = defaultDestinations();
    }

    if (settings.recording.directory.empty())
        settings.recording.directory = defaultRecordingDirectory().toStdString();
    return settings;
}

bool SettingsStore::save(const AppSettings &settings, QString *error) const
{
    return writeAtomically(settingsPath(), QJsonDocument(toJson(settings)).toJson(QJsonDocument::Indented), error);
}

std::optional<std::string> SettingsStore::loadScenes(QString *problem) const
{
    QFile file(scenesPath());
    if (!file.exists())
        return std::nullopt;
    if (!file.open(QIODevice::ReadOnly)) {
        if (problem)
            *problem = "Scenes could not be read: " + file.errorString();
        return std::nullopt;
    }
    return file.readAll().toStdString();
}

bool SettingsStore::saveScenes(const std::string &json, QString *error) const
{
    return writeAtomically(scenesPath(), QByteArray::fromStdString(json), error);
}

bool SettingsStore::writeAtomically(const QString &path, const QByteArray &bytes, QString *error) const
{
    if (!QDir().mkpath(directory_)) {
        if (error)
            *error = "Could not create " + directory_;
        return false;
    }

    // One generation of backup, in case a bad save needs undoing by hand.
    if (QFile::exists(path)) {
        QFile::remove(path + ".bak");
        QFile::copy(path, path + ".bak");
    }

    // Antivirus scanners can hold a just-written file for a moment, which makes
    // the final rename fail; a few short retries ride that out.
    QString lastError;
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (attempt > 0)
            QThread::msleep(150);
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
            return true;
        lastError = file.errorString();
    }
    if (error)
        *error = "Could not save " + path + ": " + lastError;
    return false;
}

} // namespace abyss
