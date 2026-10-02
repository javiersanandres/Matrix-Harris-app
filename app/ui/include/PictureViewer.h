#pragma once

class QPixmap;
class QWidget;

namespace ui {

    // Shows `picture` over the whole screen `parent` is on, on a dimmed
    // background (fading in), until any click or Esc. Used to look closely at
    // the screenshots of the guides and the options' help.
    void showPictureFullScreen(const QPixmap& picture, QWidget* parent);

} // namespace ui
