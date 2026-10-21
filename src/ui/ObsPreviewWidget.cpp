#ifdef ABYSS_WITH_OBS
#include "ui/ObsPreviewWidget.hpp"

#include <QHideEvent>
#include <QPaintEngine>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <graphics/graphics.h>

namespace abyss {

ObsPreviewWidget::ObsPreviewWidget(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_PaintOnScreen);
    setAutoFillBackground(false);
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

ObsPreviewWidget::~ObsPreviewWidget()
{
    destroyDisplay();
}

QPaintEngine *ObsPreviewWidget::paintEngine() const
{
    // libobs owns painting for this native child window.
    return nullptr;
}

void ObsPreviewWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
#ifdef _WIN32
    QTimer::singleShot(0, this, [this] {
        if (!display_)
            createDisplay();
        if (display_)
            obs_display_set_enabled(display_, true);
    });
#endif
}

void ObsPreviewWidget::hideEvent(QHideEvent *event)
{
    if (display_)
        obs_display_set_enabled(display_, false);
    QWidget::hideEvent(event);
}

void ObsPreviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    resizeDisplay();
}

void ObsPreviewWidget::createDisplay()
{
#ifdef _WIN32
    if (display_ || !isVisible())
        return;

    const qreal dpr = devicePixelRatioF();
    gs_init_data info{};
    info.window.hwnd = reinterpret_cast<void *>(winId());
    info.cx = std::max(1u, static_cast<unsigned>(std::lround(width() * dpr)));
    info.cy = std::max(1u, static_cast<unsigned>(std::lround(height() * dpr)));
    info.num_backbuffers = 2;
    info.format = GS_BGRA;
    info.zsformat = GS_ZS_NONE;
    info.adapter = 0;

    // Background (letterbox) color, 0xAABBGGRR.
    display_ = obs_display_create(&info, 0xFF070403u);
    if (display_)
        obs_display_add_draw_callback(display_, &ObsPreviewWidget::drawPreview, this);
#endif
}

void ObsPreviewWidget::destroyDisplay()
{
    if (!display_)
        return;
    obs_display_remove_draw_callback(display_, &ObsPreviewWidget::drawPreview, this);
    obs_display_destroy(display_);
    display_ = nullptr;
}

void ObsPreviewWidget::resizeDisplay()
{
    if (!display_)
        return;
    const qreal dpr = devicePixelRatioF();
    const auto cx = std::max(1u, static_cast<unsigned>(std::lround(width() * dpr)));
    const auto cy = std::max(1u, static_cast<unsigned>(std::lround(height() * dpr)));
    obs_display_resize(display_, cx, cy);
}

void ObsPreviewWidget::drawPreview(void *, std::uint32_t cx, std::uint32_t cy)
{
    obs_video_info ovi{};
    if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0 || cx == 0 || cy == 0)
        return;

    // Letterbox the whole canvas into the widget. Without an explicit
    // projection, libobs would draw the canvas 1:1 and crop it to the window.
    const float scale = std::min(static_cast<float>(cx) / static_cast<float>(ovi.base_width),
                                 static_cast<float>(cy) / static_cast<float>(ovi.base_height));
    const int width = static_cast<int>(std::lround(ovi.base_width * scale));
    const int height = static_cast<int>(std::lround(ovi.base_height * scale));
    const int x = (static_cast<int>(cx) - width) / 2;
    const int y = (static_cast<int>(cy) - height) / 2;

    gs_viewport_push();
    gs_projection_push();
    gs_ortho(0.0f, static_cast<float>(ovi.base_width), 0.0f, static_cast<float>(ovi.base_height), -100.0f, 100.0f);
    gs_set_viewport(x, y, width, height);

    // The main texture is the same frame that feeds the shared encoder, so
    // the preview shows exactly what every destination receives.
    obs_render_main_texture_src_color_only();

    gs_projection_pop();
    gs_viewport_pop();
}

} // namespace abyss
#endif
