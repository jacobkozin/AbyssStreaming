#pragma once
#ifdef ABYSS_WITH_OBS

#include <QWidget>
#include <cstdint>
#include <obs.h>

class QPaintEngine;
class QResizeEvent;
class QShowEvent;
class QHideEvent;

namespace abyss {

class ObsPreviewWidget final : public QWidget {
    Q_OBJECT
public:
    explicit ObsPreviewWidget(QWidget *parent = nullptr);
    ~ObsPreviewWidget() override;

    QPaintEngine *paintEngine() const override;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    static void drawPreview(void *data, std::uint32_t cx, std::uint32_t cy);
    void createDisplay();
    void destroyDisplay();
    void resizeDisplay();

    obs_display_t *display_{};
};

} // namespace abyss
#endif
