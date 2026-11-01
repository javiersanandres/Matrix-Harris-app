#pragma once

class QWidget;

namespace ui::native {

    // Shows and hides the window without the system's animation (on Windows,
    // the fade and zoom of opening and closing), so one window can take
    // another's place without a flash. Does nothing elsewhere.
    void disableTransitions(QWidget* window);

} // namespace ui::native
