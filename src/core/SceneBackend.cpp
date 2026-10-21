#include "core/SceneBackend.hpp"

namespace abyss {

const char *sourceKindLabel(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Display: return "Display Capture";
    case SourceKind::Window: return "Window Capture";
    case SourceKind::Game: return "Game Capture";
    case SourceKind::Image: return "Image";
    case SourceKind::Color: return "Color";
    case SourceKind::Text: return "Text";
    case SourceKind::VideoDevice: return "Video Capture Device";
    }
    return "Source";
}

} // namespace abyss
