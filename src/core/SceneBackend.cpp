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

SourceLayout defaultLayoutFor(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Text: return {SourcePlacement::Center, 100};
    case SourceKind::Color: return {SourcePlacement::Stretch, 100};
    case SourceKind::VideoDevice: return {SourcePlacement::BottomRight, 30};
    default: return {SourcePlacement::Fit, 100};
    }
}

} // namespace abyss
