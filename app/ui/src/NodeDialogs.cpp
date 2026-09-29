#include "NodeDialogs.h"
#include "NodeEditorWidgets.h"

#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

using namespace hypergraph_logic;

namespace ui {

    namespace {

        // Room left around the dialog inside the main window, and an estimate of
        // the window frame (title bar + borders) the dialog adds around its client.
        constexpr int WINDOW_MARGIN = 24;
        constexpr int FRAME_ALLOWANCE = 40;

        // Sizes the dialog to fit its content, but never beyond the main window
        // (or the screen, when there is no parent), and centres it there. Whatever
        // does not fit is reached by scrolling the body.
        void fitToWindow(QDialog* dialog, DialogBanner* banner, QScrollArea* scroll,
            QWidget* footer, int preferred_width)
        {
            QWidget* host = dialog->parentWidget() ? dialog->parentWidget()->window() : nullptr;
            QScreen* screen = host ? host->screen() : QGuiApplication::primaryScreen();
            QRect limit = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
            if (host) limit = limit.intersected(host->frameGeometry());
            limit.adjust(WINDOW_MARGIN, WINDOW_MARGIN, -WINDOW_MARGIN, -WINDOW_MARGIN);

            const QSize content = scroll->widget()->sizeHint();
            const int scrollbar = scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
            const int want_w = std::max(preferred_width, content.width() + scrollbar);
            const int want_h = banner->height() + content.height() + footer->sizeHint().height();

            const int max_w = limit.width();
            const int max_h = std::max(240, limit.height() - FRAME_ALLOWANCE);
            dialog->setMaximumSize(max_w, max_h);
            dialog->setMinimumWidth(std::min(preferred_width, max_w));

            const int w = std::min(want_w, max_w);
            const int h = std::min(want_h, max_h);
            dialog->resize(w, h);
            dialog->move(limit.center().x() - w / 2, limit.center().y() - (h + FRAME_ALLOWANCE) / 2);
        }

        // Lays out: banner (fixed) / scrollable body with the form and the centred
        // preview / footer (fixed) with the buttons at the bottom left. Wires the
        // live preview and the "name must not be empty" rule, sizes the dialog to
        // the main window, and returns the accept button.
        QPushButton* buildNodeDialog(QDialog* dialog, DialogBanner* banner,
            NodeAttributesForm* form, NodePreview* preview,
            const QString& preview_title, const QString& accept_text, int preferred_width)
        {
            dialog->setObjectName("NodeDialog");
            dialog->setModal(true);
            dialog->setStyleSheet(dialog_theme::styleSheet());

            auto* root = new QVBoxLayout(dialog);
            root->setContentsMargins(0, 0, 0, 0);
            root->setSpacing(0);
            root->addWidget(banner);

            // ── Scrollable body ───────────────────────────────────────────────────
            auto* body_widget = new QWidget;
            body_widget->setObjectName("dialogBody");
            auto* body = new QVBoxLayout(body_widget);
            body->setContentsMargins(22, 16, 22, 12);
            body->setSpacing(12);
            body->addWidget(form);

            auto* title = new QLabel(preview_title.toUpper());
            title->setObjectName("previewTitle");
            title->setAlignment(Qt::AlignCenter);
            body->addWidget(title);
            auto* preview_row = new QHBoxLayout;
            preview_row->addStretch();
            preview_row->addWidget(preview);
            preview_row->addStretch();
            body->addLayout(preview_row);

            auto* scroll = new QScrollArea;
            scroll->setObjectName("dialogScroll");
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            scroll->setWidget(body_widget);
            scroll->viewport()->setAutoFillBackground(false);
            body_widget->setAutoFillBackground(false);
            root->addWidget(scroll, 1);

            // ── Footer: buttons at the bottom left ────────────────────────────────
            auto* footer = new QFrame;
            footer->setObjectName("dialogFooter");
            auto* buttons = new QHBoxLayout(footer);
            buttons->setContentsMargins(22, 12, 22, 16);
            buttons->setSpacing(10);

            auto* accept = new QPushButton(accept_text);
            accept->setObjectName("primary");
            accept->setDefault(true);
            accept->setCursor(Qt::PointingHandCursor);
            auto* cancel = new QPushButton(QStringLiteral("Cancelar"));
            cancel->setObjectName("secondary");
            cancel->setCursor(Qt::PointingHandCursor);
            buttons->addWidget(accept);
            buttons->addWidget(cancel);
            buttons->addStretch();
            root->addWidget(footer);

            QObject::connect(accept, &QPushButton::clicked, dialog, &QDialog::accept);
            QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);

            auto refresh = [form, preview, accept] {
                const NodeAttributes a = form->attributes();
                preview->setAttributes(a);
                accept->setEnabled(!a.name.empty());
                accept->setToolTip(a.name.empty() ? QStringLiteral("El nombre no puede estar vacío") : QString());
            };
            QObject::connect(form, &NodeAttributesForm::attributesChanged, dialog, refresh);
            refresh();

            // The footer only needs a divider when the body actually scrolls.
            auto divider = [footer, scroll] {
                const bool scrolls = scroll->verticalScrollBar()->maximum() > 0;
                footer->setProperty("divided", scrolls);
                dialog_theme::repolish(footer);
            };
            QObject::connect(scroll->verticalScrollBar(), &QScrollBar::rangeChanged, dialog, divider);

            fitToWindow(dialog, banner, scroll, footer, preferred_width);
            divider();
            return accept;
        }

    } // namespace

    // ============================================================================
    // NodeDialog
    // ============================================================================

    NodeDialog::NodeDialog(Mode mode, const NodeAttributes& initial,
        const QList<QColor>& recent_colours, QWidget* parent)
        : QDialog(parent)
    {
        const bool create = mode == Mode::Create;
        setWindowTitle(create ? QStringLiteral("Crear nodo") : QStringLiteral("Propiedades del nodo"));

        auto* banner = new DialogBanner(
            create ? DialogBanner::Glyph::Create : DialogBanner::Glyph::Edit,
            create ? QStringLiteral("Nuevo nodo") : QStringLiteral("Propiedades del nodo"),
            create ? QStringLiteral("Dale un nombre y un aspecto a tu nuevo nodo")
                   : QStringLiteral("Modifica cualquier atributo de «%1»").arg(QString::fromStdString(initial.name)),
            this);

        form_ = new NodeAttributesForm(initial, recent_colours, this);
        preview_ = new NodePreview(this);
        preview_->setFixedWidth(340);

        accept_ = buildNodeDialog(this, banner, form_, preview_, QStringLiteral("Vista previa"),
            create ? QStringLiteral("Crear") : QStringLiteral("Actualizar"), 580);

        form_->focusName();
    }

    NodeAttributes NodeDialog::attributes() const { return form_->attributes(); }
    QList<QColor> NodeDialog::pickedColours() const { return form_->pickedColours(); }

    // ============================================================================
    // FuseNodesDialog
    // ============================================================================

    FuseNodesDialog::FuseNodesDialog(const NodeAttributes& first, const NodeAttributes& second,
        const QList<QColor>& recent_colours, QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Fusionar nodos"));

        const QString first_title = QStringLiteral("«%1»").arg(QString::fromStdString(first.name));
        const QString second_title = QStringLiteral("«%1»").arg(QString::fromStdString(second.name));

        auto* banner = new DialogBanner(DialogBanner::Glyph::Fuse,
            QStringLiteral("Fusionar nodos"),
            QStringLiteral("Elige qué valor prevalece en cada campo, o escríbelo tú mismo"),
            this);

        // The fused node starts as a copy of the first node chosen.
        form_ = new NodeAttributesForm(first, recent_colours,
            first, first_title, second, second_title, this);
        preview_ = new NodePreview(this);
        preview_->setFixedWidth(360);

        accept_ = buildNodeDialog(this, banner, form_, preview_,
            QStringLiteral("Vista previa del nodo fusionado"), QStringLiteral("Fusionar"), 1000);

        form_->focusName();
    }

    NodeAttributes FuseNodesDialog::attributes() const { return form_->attributes(); }
    QList<QColor> FuseNodesDialog::pickedColours() const { return form_->pickedColours(); }

} // namespace ui
