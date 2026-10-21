#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace abyss {

// UTF-8 <-> std::filesystem::path without the C++20-deprecated u8path().
// libobs and the settings files speak UTF-8; Windows paths are UTF-16.
inline std::filesystem::path pathFromUtf8(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

inline std::string pathToUtf8(const std::filesystem::path &path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

} // namespace abyss
