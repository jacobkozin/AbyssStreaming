#include "platform/HdrMonitor.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cwchar>
#include <vector>
#endif

namespace abyss {

#ifdef _WIN32

namespace {

template <typename Info>
bool queryTarget(const DISPLAYCONFIG_PATH_INFO &path, DISPLAYCONFIG_DEVICE_INFO_TYPE type, Info &info)
{
    info.header.type = type;
    info.header.size = sizeof(info);
    info.header.adapterId = path.targetInfo.adapterId;
    info.header.id = path.targetInfo.id;
    return DisplayConfigGetDeviceInfo(&info.header) == ERROR_SUCCESS;
}

bool isPrimary(const DISPLAYCONFIG_PATH_INFO &path, const wchar_t *primaryDevice)
{
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    return DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS &&
           std::wcscmp(source.viewGdiDeviceName, primaryDevice) == 0;
}

} // namespace

std::optional<float> hdrMonitorSdrWhiteNits()
{
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG result = ERROR_INSUFFICIENT_BUFFER;
    // A monitor can be plugged in between the two calls; then ask again.
    while (result == ERROR_INSUFFICIENT_BUFFER) {
        UINT32 pathCount = 0;
        UINT32 modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
            return std::nullopt;
        paths.resize(pathCount);
        modes.resize(modeCount);
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        paths.resize(pathCount);
    }
    if (result != ERROR_SUCCESS)
        return std::nullopt;

    MONITORINFOEXW primary{};
    primary.cbSize = sizeof(primary);
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary);

    std::optional<float> first;
    for (const auto &path : paths) {
        // Automatic color management also turns "advanced color" on for SDR
        // monitors; it reports itself as wide color enforced.
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
        if (!queryTarget(path, DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, color) ||
            !color.advancedColorEnabled || color.wideColorEnforced)
            continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        if (!queryTarget(path, DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, white))
            continue;

        const float nits = static_cast<float>(white.SDRWhiteLevel) * 80.0f / 1000.0f; // 1000 means 80 nits
        if (isPrimary(path, primary.szDevice))
            return nits;
        if (!first)
            first = nits;
    }
    return first;
}

#else

std::optional<float> hdrMonitorSdrWhiteNits()
{
    return std::nullopt;
}

#endif

} // namespace abyss
