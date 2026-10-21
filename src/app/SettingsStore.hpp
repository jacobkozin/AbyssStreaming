#pragma once

#include "core/AppSettings.hpp"
#include <QJsonObject>
#include <QString>
#include <optional>
#include <string>

namespace abyss {

// Reads and writes settings.json and scenes.json. Writes go through
// QSaveFile, so a crash mid-write never leaves a half-written file, and the
// previous version is kept as *.bak. Stream keys are not part of AppSettings'
// JSON form at all; they live in the ISecretStore.
class SettingsStore {
public:
    explicit SettingsStore(QString directory);

    QString directory() const { return directory_; }
    QString settingsPath() const;
    QString scenesPath() const;

    // Missing file: defaults. Unreadable file: defaults plus a problem
    // message, and the bad file is kept as settings.json.corrupt.
    AppSettings load(QString *problem = nullptr) const;
    bool save(const AppSettings &settings, QString *error = nullptr) const;

    std::optional<std::string> loadScenes(QString *problem = nullptr) const;
    bool saveScenes(const std::string &json, QString *error = nullptr) const;

    static QJsonObject toJson(const AppSettings &settings);
    static AppSettings fromJson(const QJsonObject &json);

private:
    bool writeAtomically(const QString &path, const QByteArray &bytes, QString *error) const;

    QString directory_;
};

} // namespace abyss
