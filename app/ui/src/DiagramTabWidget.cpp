#include "DiagramTabWidget.h"
#include "DiagramView.h"
#include "UiStyle.h"

#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {

    DiagramTabWidget::DiagramTabWidget(QGraphicsScene* shared_scene,
        const QString& name,
        bool            is_joint,
        QWidget* parent)
        : QWidget(parent)
        , is_joint_(is_joint)
        , name_(name)
    {
        setFixedSize(TAB_WIDTH, TAB_HEIGHT);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet(QStringLiteral(R"(
QGraphicsView#miniature { background: #FAFBFD; border: 1px solid #EEF0F5; border-radius: 8px; }
QLabel#tabName { color: #3A4050; font-weight: 600; }
QLabel#tabName[active="true"] { color: #4F46E5; font-weight: 700; }
QLineEdit#tabNameEdit {
    border: 1px solid #6366F1; border-radius: 6px; padding: 1px 6px; background: white;
    color: #1F2330; selection-background-color: #6366F1;
}
QToolButton#tabTrash { border: none; border-radius: 6px; background: transparent; }
QToolButton#tabTrash:hover { background: #FDECEC; }
)"));

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(8, 8, 8, 6);
        outer->setSpacing(5);

        // Miniature: zoom (Ctrl+wheel), scroll (wheel) and pan (drag) right
        // here, without selecting the tab; clicks on the rest of the card select it.
        miniature_view_ = new DiagramView(shared_scene, this);
        miniature_view_->setObjectName("miniature");
        miniature_view_->setThumbnailMode(true);
        miniature_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        miniature_view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        outer->addWidget(miniature_view_, 1);
        // Re-fit once the layout sizes it. The viewport (not the view) is what
        // fitInView measures, and it is resized only after the view itself.
        miniature_view_->viewport()->installEventFilter(this);
        if (shared_scene)
            connect(shared_scene, &QGraphicsScene::sceneRectChanged, this, &DiagramTabWidget::refitMiniature);

        // Bottom row: [room for the bin] ([joint glyph] name, centred) [bin].
        // The bin's slot is kept even while it is hidden, and mirrored on the
        // left, so the name stays centred and never shifts on hover.
        auto* bottom = new QHBoxLayout;
        bottom->setContentsMargins(0, 0, 0, 0);
        bottom->setSpacing(4);
        bottom->addSpacing(NAME_HEIGHT);

        name_box_ = new QWidget(this);
        auto* centred = new QHBoxLayout(name_box_);
        centred->setContentsMargins(0, 0, 0, 0);
        centred->setSpacing(5);
        centred->addStretch();
        if (is_joint_) {
            glyph_ = new QLabel(name_box_);
            glyph_->setPixmap(style::icon(style::Icon::Joint).pixmap(QSize(16, 16), devicePixelRatioF()));
            centred->addWidget(glyph_);
        }
        name_label_ = new QLabel(name_box_);
        name_label_->setObjectName("tabName");
        name_label_->setFixedHeight(NAME_HEIGHT);
        name_label_->installEventFilter(this);
        centred->addWidget(name_label_);
        centred->addStretch();
        bottom->addWidget(name_box_, 1);

        name_edit_ = new QLineEdit(name, this);
        name_edit_->setObjectName("tabNameEdit");
        name_edit_->setAlignment(Qt::AlignCenter);
        name_edit_->setFixedHeight(NAME_HEIGHT);
        name_edit_->hide();
        name_edit_->installEventFilter(this);
        bottom->addWidget(name_edit_, 1);
        connect(name_edit_, &QLineEdit::editingFinished, this, [this] { finishRename(true); });

        trash_btn_ = new QToolButton(this);
        trash_btn_->setObjectName("tabTrash");
        trash_btn_->setIcon(style::icon(style::Icon::Trash));
        trash_btn_->setIconSize(QSize(16, 16));
        trash_btn_->setFixedSize(NAME_HEIGHT, NAME_HEIGHT);
        trash_btn_->setToolTip(QStringLiteral("Eliminar esquema"));
        QSizePolicy keep = trash_btn_->sizePolicy();
        keep.setRetainSizeWhenHidden(true);
        trash_btn_->setSizePolicy(keep);
        trash_btn_->hide();
        bottom->addWidget(trash_btn_);
        connect(trash_btn_, &QToolButton::clicked, this, &DiagramTabWidget::removeRequested);

        outer->addLayout(bottom);
        refreshName();
    }

    void DiagramTabWidget::setScene(QGraphicsScene* scene) {
        if (QGraphicsScene* old = miniature_view_->scene())
            disconnect(old, nullptr, this, nullptr);
        miniature_view_->setScene(scene);
        if (scene)
            connect(scene, &QGraphicsScene::sceneRectChanged, this, &DiagramTabWidget::refitMiniature);
        refitMiniature();
    }

    void DiagramTabWidget::setActive(bool active) {
        // Leaving a tab closes its name editor, keeping what was typed.
        if (!active) finishRename(true);
        active_ = active;
        name_label_->setProperty("active", active);
        name_label_->style()->unpolish(name_label_);
        name_label_->style()->polish(name_label_);
        refreshName();
        update();
    }

    void DiagramTabWidget::setDisplayName(const QString& name) {
        name_ = name;
        name_edit_->setText(name);
        refreshName();
    }

    void DiagramTabWidget::refreshName() {
        // Everything the row holds besides the name: margins, the two bin
        // slots, spacing and, on the joint tab, its glyph.
        const int room = TAB_WIDTH - 16 - 2 * NAME_HEIGHT - 16 - (is_joint_ ? 21 : 0);
        name_label_->setText(name_label_->fontMetrics().elidedText(name_, Qt::ElideRight, room));
        name_label_->setToolTip(is_joint_ ? name_ : name_ + QStringLiteral("\nDoble clic para renombrar"));
    }

    void DiagramTabWidget::refitMiniature() {
        QGraphicsScene* scene = miniature_view_->scene();
        if (scene && !scene->sceneRect().isEmpty())
            miniature_view_->fitWithMargin(scene->sceneRect(), 6.0);
    }

    void DiagramTabWidget::resizeEvent(QResizeEvent* event) {
        QWidget::resizeEvent(event);
        refreshName();
    }

    void DiagramTabWidget::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);

        QColor border = style::palette::line;
        QColor fill = Qt::white;
        double width = 1.0;
        if (active_) {
            border = style::palette::accent;
            fill = QColor(0xF7, 0xF7, 0xFF);
            width = 2.0;
        }
        else if (hovered_) {
            border = QColor(0xC7, 0xC9, 0xF9);
        }
        p.setPen(QPen(border, width));
        p.setBrush(fill);
        p.drawRoundedRect(card, 12, 12);

        if (active_) {
            // Accent strip along the top edge of the active card.
            QLinearGradient g(card.topLeft(), card.topRight());
            g.setColorAt(0, style::palette::accent);
            g.setColorAt(1, style::palette::violet);
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawRoundedRect(QRectF(card.left() + 14, card.top() + 0.5, card.width() - 28, 3.0), 1.5, 1.5);
        }
    }

    void DiagramTabWidget::mousePressEvent(QMouseEvent* event) {
        if (event->button() == Qt::LeftButton) emit clicked();
    }

    void DiagramTabWidget::enterEvent(QEnterEvent* event) {
        QWidget::enterEvent(event);
        hovered_ = true;
        if (!is_joint_ && name_edit_->isHidden()) trash_btn_->show();
        refreshName();
        update();
    }

    void DiagramTabWidget::leaveEvent(QEvent* event) {
        QWidget::leaveEvent(event);
        hovered_ = false;
        trash_btn_->hide();
        refreshName();
        update();
    }

    bool DiagramTabWidget::eventFilter(QObject* watched, QEvent* event) {
        if (watched == miniature_view_->viewport() && event->type() == QEvent::Resize) {
            refitMiniature();
            return false;
        }
        if (watched == name_label_ && event->type() == QEvent::MouseButtonDblClick && !is_joint_) {
            startRename();
            return true;
        }
        if (watched == name_edit_ && event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            finishRename(false);
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

    void DiagramTabWidget::startRename() {
        if (is_joint_) return;
        name_edit_->setText(name_);
        name_box_->hide();
        trash_btn_->hide();
        name_edit_->show();
        name_edit_->setFocus();
        name_edit_->selectAll();
    }

    void DiagramTabWidget::finishRename(bool commit) {
        if (name_edit_->isHidden()) return; // editingFinished also fires on hide
        name_edit_->hide();
        name_box_->show();
        if (commit) {
            QString new_name = name_edit_->text().trimmed();
            if (new_name.isEmpty()) new_name = QStringLiteral("Esquema nuevo");
            if (new_name != name_) {
                name_ = new_name;
                emit renamed(new_name);
            }
        }
        refreshName();
    }

} // namespace ui
