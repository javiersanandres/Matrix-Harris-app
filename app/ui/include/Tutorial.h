#pragma once

#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

namespace app_logic {
    class Project;
}

class QFrame;
class QLabel;
class QPushButton;
class QSettings;
class QVariantAnimation;

namespace ui::tutorial {

    // ============================================================================
    // Introductory tour
    //
    // A guided tour over the real window: everything is dimmed except the part
    // being explained, which is outlined, and a card next to it says what it
    // does, often with a picture of it in use:
    //
    //   ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
    //   ░░ ┏━━━━━━━━━━━━━━━━━━━━┓ ░░░░░░░░░░░░░░░░░░░░
    //   ░░ ┃  highlighted part  ┃ ░░░░░░░░░░░░░░░░░░░░
    //   ░░ ┗━━━━━━━━━━━━━━━━━━━━┛ ░░░░░░░░░░░░░░░░░░░░
    //   ░░ ┌───────────────────────────────────┐ ░░░░░
    //   ░░ │ ━━━━━━━━━━━──────────  (progress) │ ░░░░░
    //   ░░ │ 3 de 18             Saltar tutorial │ ░░░░░
    //   ░░ │ Title                              │ ░░░░░
    //   ░░ │ Text …                             │ ░░░░░
    //   ░░ │ [ picture ]                        │ ░░░░░
    //   ░░ │              [‹ Anterior] [Siguiente ›] │
    //   ░░ └───────────────────────────────────┘ ░░░░░
    //
    // Siguiente / Anterior (or the arrow keys, Enter) walk through it; "Saltar
    // tutorial" or Esc leave at any time.
    //
    // The outlined parts work as usual, so the user can try what each step
    // explains (on an example project, see MainWindow::runTutorial); the rest
    // of the window does not react while the tour is on.
    // ============================================================================

    // Parts of the main window a step can point at.
    enum class Target {
        Canvas,            // the editing view
        TabStrip,          // the diagrams' tabs
        FirstTabMiniature, // the live miniature of the first tab
        FirstTabName,      // the name under it
        AddTab,            // the "+" after the tabs
        JointTab,          // the joint diagram's tab
        MinimizePanel,     // the floating "Minimizar cruces" panel
        ZoomLabel,         // the zoom shown in the status bar
        FileMenu,          // menu bar titles
        EditMenu,
        HelpMenu,
    };

    // What the editing view should show when a step starts.
    enum class View {
        Keep,         // whatever it shows
        FirstDiagram, // the project's first diagram
        Joint,        // the joint diagram
    };

    struct Step {
        QString title;
        QString text;                // rich text
        QString image;               // resource path, or empty
        std::vector<Target> targets; // outlined (and usable) together; none: a centred card
        View view = View::Keep;
    };

    // The steps of the introductory tour.
    std::vector<Step> introSteps();

    // Where a target is, in the window's coordinates (an empty rect when it is
    // not on screen: the card is then centred).
    using Locator = std::function<QRect(Target)>;

    // Puts a step's view on screen.
    using Shower = std::function<void(View)>;

    // The project the tour lets the user practise on: two small diagrams of
    // numbered boxes, the first already in the joint diagram. It has no file
    // and nothing to undo or save. No other project may exist when it is
    // made (there is only one joint diagram at a time).
    std::unique_ptr<app_logic::Project> exampleProject();

    class TutorialOverlay : public QWidget {
        Q_OBJECT
    public:
        // Covers `window` (its parent) until the tour ends; deletes itself then.
        TutorialOverlay(QWidget* window, std::vector<Step> steps, Locator locate, Shower show = {});

        void start();

    signals:
        void finished(bool completed); // false: skipped

    protected:
        void paintEvent(QPaintEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;

    private:
        void go(int index);
        void refresh(bool animate);
        QPoint cardPosition(const QRect& focus, QSize card) const;
        std::vector<QRectF> holesFor(const Step& step) const;
        // The outlines on screen now (part way through a morph).
        std::vector<QRectF> shownHoles() const;
        void finish(bool completed);
        // Only the dimmed part (and the card) take the mouse: the outlined
        // parts underneath stay usable.
        void updateMask();
        // Follows the outlined parts when they move or change size (a new tab,
        // the panel dragged elsewhere).
        void follow();

        std::vector<Step> steps_;
        Locator locate_;
        Shower show_;
        QTimer follow_timer_;
        bool card_placed_ = false; // the first step puts the card in place, without gliding
        int current_ = -1;

        // The outlined parts, morphing from `from_` to `to_` between steps.
        std::vector<QRectF> from_, to_;
        double morph_ = 1.0;
        QVariantAnimation* morph_anim_;

        QFrame* card_;
        QWidget* progress_;
        QLabel* counter_;
        QLabel* title_;
        QLabel* text_;
        QLabel* picture_;
        QPushButton* prev_;
        QPushButton* next_;
    };

    // Whether the tour still has to be shown on starting the application
    // (it has not been finished or skipped yet).
    bool pendingAtStartup(QSettings& settings);
    void markSeen(QSettings& settings);

} // namespace ui::tutorial
