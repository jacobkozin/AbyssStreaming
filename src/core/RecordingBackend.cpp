#include "core/RecordingBackend.hpp"

#include <cstdio>

namespace abyss {

std::string_view recordingExtension(RecordingFormat format)
{
    return format == RecordingFormat::Mp4 ? "mp4" : "mkv";
}

std::string_view recordingFormatId(RecordingFormat format)
{
    return recordingExtension(format);
}

RecordingFormat recordingFormatFromId(std::string_view id)
{
    return id == "mp4" ? RecordingFormat::Mp4 : RecordingFormat::Mkv;
}

std::string recordingFileName(std::tm localTime, RecordingFormat format)
{
    char stamp[80]{}; // room for any int the compiler can imagine in std::tm
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d-%02d-%02d", localTime.tm_year + 1900,
                  localTime.tm_mon + 1, localTime.tm_mday, localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
    return std::string("Abyss Stream ") + stamp + "." + std::string(recordingExtension(format));
}

} // namespace abyss
