#include "DiagramTabWidget.h"
#include "DiagramView.h"

#include <QGraphicsScene>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSizePolicy>

namespace ui {

    DiagramTabWidget::DiagramTabWidget(QGraphicsScene* shared_scene,
        const QString& name,
        bool            is_joint,
        QWidget* parent)
        : QWidget(parent)
        , is_joint_(is_joint)
        , active_(false)
    {
        setFixedSize(TAB_WIDTH, TAB_HEIGHT);
        setStyleSheet("border: 1px solid #ccc; border-radius: 4px; "
            "background: #f5f5f5;");

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(4, 4, 4, 4);
        outer->setSpacing(2);

        // Miniature view.
        miniature_view_ = new DiagramView(shared_scene, this);
        miniature_view_->setInteractive(false);
        miniature_view_->setFixedHeight(TAB_HEIGHT - NAME_HEIGHT - 14);
        miniature_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        miniature_view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        if (shared_scene && !shared_scene->sceneRect().isEmpty())
            miniature_view_->fitWithMargin(shared_scene->sceneRect());
        outer->addWidget(miniature_view_);

        // Bottom row: name label/edit + trash button.
        auto* bottom = new QHBoxLayout;
        bottom->setContentsMargins(0, 0, 0, 0);
        bottom->setSpacing(2);

        name_label_ = new QLabel(name, this);
        name_label_->setAlignment(Qt::AlignCenter);
        name_label_->setFixedHeight(NAME_HEIGHT);
        bottom->addWidget(name_label_, 1);

        name_edit_ = new QLineEdit(name, this);
        name_edit_->setAlignment(Qt::AlignCenter);
        name_edit_->setFixedHeight(NAME_HEIGHT);
        name_edit_->hide();
        bottom->addWidget(name_edit_, 1);

        // Trash button — hidden for the joint tab (it cannot be deleted).
        trash_btn_ = new QPushButton("🗑", this);
        trash_btn_->setFixedSize(NAME_HEIGHT, NAME_HEIGHT);
        trash_btn_->setToolTip("Eliminar diagrama");
        trash_btn_->setFlat(true);
        trash_btn_->setVisible(!is_joint_);
        bottom->addWidget(trash_btn_);

        outer->addLayout(bottom);

        connect(name_edit_, &QLineEdit::editingFinished,
            this, &DiagramTabWidget::onNameEditFinished);
        connect(trash_btn_, &QPushButton::clicked,
            this, &DiagramTabWidget::removeRequested);
    }

    void DiagramTabWidget::setActive(bool active) {
        active_ = active;
        if (active) {
            miniature_view_->hide();
            name_label_->hide();
            trash_btn_->hide();
            name_edit_->show();
            name_edit_->setFocus();
            name_edit_->selectAll();
            setStyleSheet("border: 2px solid #3399ff; border-radius: 4px; "
                "background: #e8f4ff;");
        }
        else {
            miniature_view_->show();
            name_label_->show();
            name_edit_->hide();
            if (!is_joint_) trash_btn_->show();
            setStyleSheet("border: 1px solid #ccc; border-radius: 4px; "
                "background: #f5f5f5;");
            // Re-fit the miniature after possible scene changes.
            if (miniature_view_->scene() &&
                !miniature_view_->scene()->sceneRect().isEmpty())
                miniature_view_->fitWithMargin(
                    miniature_view_->scene()->sceneRect());
        }
    }

    void DiagramTabWidget::setDisplayName(const QString& name) {
        name_label_->setText(name);
        name_edit_->setText(name);
    }

    QString DiagramTabWidget::displayName() const {
        return name_label_->text();
    }

    void DiagramTabWidget::mousePressEvent(QMouseEvent* event) {
        // Emit clicked() when pressing anywhere on the tab except the trash button.
        // The trash button has its own signal so it stops propagation itself.
        if (event->button() == Qt::LeftButton)
            emit clicked();
    }

    void DiagramTabWidget::onNameEditFinished() {
        QString new_name = name_edit_->text().trimmed();
        if (new_name.isEmpty()) new_name = "Diagrama nuevo";
        name_label_->setText(new_name);
        emit renamed(new_name);
    }

} // namespace ui