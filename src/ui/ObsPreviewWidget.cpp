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

    display_ = obs_display_create(&info, 0x00000000u);
    if (display_) {
        obs_display_add_draw_callback(display_, &ObsPreviewWidget::drawPreview, this);
        obs_display_set_enabled(display_, true);
    }
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

void ObsPreviewWidget::drawPreview(void *, std::uint32_t, std::uint32_t)
{
    // The main texture is the same scene mix that feeds the shared encoder,
    // so the preview and every multistream destination see the same frame.
    obs_render_main_texture();
}

} // namespace abyss
#endif
