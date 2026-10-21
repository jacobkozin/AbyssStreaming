#include "app/AppPaths.hpp"

#include <QDir>
#include <QStandardPaths>

namespace abyss {

QString appDataDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString defaultRecordingDirectory()
{
    QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty())
        videos = QDir::homePath();
    return QDir(videos).filePath("Abyss Stream");
}

} // namespace abyss
