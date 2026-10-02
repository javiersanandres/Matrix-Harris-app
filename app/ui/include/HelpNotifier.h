#pragma once

#include <QObject>
#include <QStringList>
#include <QWidget>

#include <map>

class QAction;
class QMenu;
class QMenuBar;
class QSettings;
class QVariantAnimation;

namespace ui {

    // ============================================================================
    // NotificationDot
    //
    // A small indigo dot with a soft pulse around it, laid over another widget
    // (it lets clicks through) to say "there is something new here".
    // ============================================================================
    class NotificationDot : public QWidget {
    public:
        explicit NotificationDot(QWidget* parent);

        // Puts the dot's centre at `point` (in the parent's coordinates).
        void centreAt(QPoint point);

        static constexpr int SIZE = 20;

    protected:
        void paintEvent(QPaintEvent* event) override;
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;

    private:
        QVariantAnimation* pulse_;
        double phase_ = 0.0; // 0..1 through one pulse
    };

    // ============================================================================
    // HelpNotifier
    //
    // Points the user to an entry of the Help menu after they dismissed
    // something that also lives there (a notice they asked not to see again, a
    // tour they skipped):
    //
    //   - a pulsing dot sits on the menu's title in the menu bar;
    //   - opening the menu hides it and shows a dot next to each flagged entry;
    //   - closing the menu clears them all: the user has seen where they are.
    //
    // Flagged entries are remembered in the user's settings until the menu
    // has been opened, so a flag survives restarting the application.
    // ============================================================================
    class HelpNotifier : public QObject {
    public:
        HelpNotifier(QMenuBar* bar, QMenu* help_menu, QObject* parent = nullptr);

        // Makes `action` (an entry of the Help menu) flaggable as `key`.
        void addEntry(const QString& key, QAction* action);

        // Flags that entry until the menu is next opened and closed.
        void flag(const QString& key);

        QStringList flagged() const { return flagged_; }

        // The flags kept in `settings` (see the class comment).
        static QStringList storedFlags(QSettings& settings);
        static void storeFlags(QSettings& settings, const QStringList& keys);

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override;

    private:
        void placeBarDot();
        void placeEntryDots();
        void clear();
        void save() const;

        QMenuBar* bar_;
        QMenu* menu_;
        std::map<QString, QAction*> entries_;
        QStringList flagged_;
        NotificationDot* bar_dot_;
        std::map<QString, NotificationDot*> entry_dots_;
    };

} // namespace ui
