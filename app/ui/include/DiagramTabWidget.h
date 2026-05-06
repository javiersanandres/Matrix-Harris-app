#pragma once

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QPushButton>

class QGraphicsScene;

namespace ui {

    class DiagramView;

    // ============================================================================
    // DiagramTabWidget
    //
    // Represents one tab in the horizontal tab bar.
    //
    // Inactive state: small non-interactive DiagramView (miniature) + name label
    //                 below it + a trash button (🗑) to the right of the name.
    //                 Clicking the miniature OR the name label activates the tab.
    //                 Clicking the trash button emits removeRequested().
    //
    // Active state:   highlighted background + editable QLineEdit for the name.
    //                 The miniature is hidden.
    //
    // The miniature view shares the same QGraphicsScene as the central editing
    // view, so it always reflects the current graph state for free.
    // ============================================================================
    class DiagramTabWidget : public QWidget {
        Q_OBJECT

    public:
        explicit DiagramTabWidget(QGraphicsScene* shared_scene,
            const QString& name,
            bool            is_joint,
            QWidget* parent = nullptr);

        void    setActive(bool active);
        bool    isActive() const { return active_; }

        void    setDisplayName(const QString& name);
        QString displayName() const;

        bool isJoint() const { return is_joint_; }

    signals:
        void renamed(const QString& new_name);   // user committed a rename
        void clicked();                          // tab selected
        void removeRequested();                  // trash button pressed

    protected:
        void mousePressEvent(QMouseEvent* event) override;

    private slots:
        void onNameEditFinished();

    private:
        bool is_joint_;
        bool active_ = false;

        DiagramView* miniature_view_;
        QLabel* name_label_;
        QLineEdit* name_edit_;
        QPushButton* trash_btn_;   // hidden for the joint tab

        static constexpr int TAB_WIDTH = 190;
        static constexpr int TAB_HEIGHT = 148;
        static constexpr int NAME_HEIGHT = 24;
    };

} // namespace ui