#pragma once

#include "core/IStreamBackend.hpp"
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

namespace abyss {

enum class RecordingFormat {
    Mkv, // survives crashes and power loss; the default
    Mp4, // fragmented MP4, also crash-tolerant and plays in more editors
};

struct RecordingOptions {
    std::string directory;
    RecordingFormat format{RecordingFormat::Mkv};
};

struct RecordingStatus {
    bool active{false};
    std::string path;
    std::uint64_t bytesWritten{0};
    std::int64_t elapsedMs{0};
    std::string message; // last error or stop reason
};

class IRecordingBackend {
public:
    virtual ~IRecordingBackend() = default;
    virtual StartResult start(const RecordingOptions &options) = 0;
    virtual void stop() = 0;
    virtual RecordingStatus status() const = 0;
};

std::string_view recordingExtension(RecordingFormat format);
std::string_view recordingFormatId(RecordingFormat format);
RecordingFormat recordingFormatFromId(std::string_view id);
// "Abyss Stream 2026-10-20 19-45-03.mkv" for the given local time.
std::string recordingFileName(std::tm localTime, RecordingFormat format);

} // namespace abyss
