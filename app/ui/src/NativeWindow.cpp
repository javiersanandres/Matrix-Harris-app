#include "NativeWindow.h"

#include <QWidget>

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace ui::native {

    void disableTransitions(QWidget* window) {
#ifdef Q_OS_WIN
        const BOOL disabled = TRUE;
        // winId() creates the native window if it does not exist yet.
        DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), DWMWA_TRANSITIONS_FORCEDISABLED,
            &disabled, sizeof(disabled));
#else
        (void)window;
#endif
    }

} // namespace ui::native
