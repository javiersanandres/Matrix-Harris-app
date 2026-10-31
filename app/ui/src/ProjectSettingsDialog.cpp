#include "ProjectSettingsDialog.h"
#include "AppDialogs.h"
#include "LayoutPreview.h"
#include "SettingsWidgets.h"
#include "NodeEditorWidgets.h"
#include "NodeVisuals.h"
#include "UiStyle.h"

#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include <nlohmann/json.hpp>

using namespace hypergraph_logic;
using app_logic::ProjectSettings;

namespace ui {

    namespace nv = node_visuals;

    // ============================================================================
    // app_defaults
    // ============================================================================

    namespace app_defaults {
        namespace {
            const QString KEY = QStringLiteral("project/defaults"); // the settings' JSON (ProjectSettings::toJSON)
        }

        ProjectSettings load(QSettings& settings) {
            const QString text = settings.value(KEY).toString();
            if (text.isEmpty()) return ProjectSettings();
            try {
                return ProjectSettings::fromJSON(nlohmann::json::parse(text.toStdString()));
            }
            catch (const std::exception&) {
                return ProjectSettings(); // something else wrote there: the built-in ones
            }
        }

        void save(QSettings& settings, const ProjectSettings& defaults) {
            nlohmann::json j;
            defaults.clamped().toJSON(j);
            settings.setValue(KEY, QString::fromStdString(j.dump()));
        }

        ProjectSettings load() {
            QSettings settings;
            return load(settings);
        }

        void save(const ProjectSettings& defaults) {
            QSettings settings;
            save(settings, defaults);
        }
    } // namespace app_defaults

    // ============================================================================
    // ProjectSettingsDialog
    // ============================================================================

    namespace {
        QLabel* sectionTitle(const QString& text) {
            auto* label = new QLabel(text.toUpper());
            label->setObjectName("sectionTitle");
            return label;
        }

        QLabel* fieldLabel(const QString& text) {
            auto* label = new QLabel(text);
            label->setObjectName("fieldLabel");
            return label;
        }

        QLabel* hint(const QString& text) {
            auto* label = new QLabel(text);
            label->setObjectName("hint");
            label->setWordWrap(true);
            return label;
        }

        QString shapeName(NodeShape shape) {
            switch (shape) {
            case NodeShape::Circle:           return QStringLiteral("Círculo");
            case NodeShape::Rhombus:          return QStringLiteral("Rombo");
            case NodeShape::RoundedRectangle: return QStringLiteral("Redondeado");
            default:                          return QStringLiteral("Rectángulo");
            }
        }

        // A shape drawn plainly, for its tile and its row of default sizes.
        QPixmap shapeIcon(NodeShape shape, const QSize& size, qreal dpr) {
            NodeAttributes a;
            a.shape = shape;
            a.colour = { 0xEE, 0xF0, 0xFF, 255 };
            return renderNodeIcon(a, size, dpr);
        }

        QSpinBox* lengthField(double min, double max, const QString& what) {
            auto* spin = new QSpinBox;
            spin->setObjectName("settingSpin");
            spin->setRange(static_cast<int>(min), static_cast<int>(max));
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            spin->setKeyboardTracking(false); // one change per typed value, not per digit
            spin->setAlignment(Qt::AlignRight);
            spin->setFixedWidth(76);
            spin->setToolTip(QStringLiteral("%1, en píxeles (de %2 a %3)")
                .arg(what).arg(static_cast<int>(min)).arg(static_cast<int>(max)));
            return spin;
        }

        QLabel* unit(const QString& text) {
            auto* label = new QLabel(text);
            label->setObjectName("unit");
            return label;
        }

        // "↺": back to what the project had when the window opened.
        QToolButton* revertButton() {
            auto* button = new QToolButton;
            button->setObjectName("previewReset");
            button->setText(QStringLiteral("↺"));
            button->setCursor(Qt::PointingHandCursor);
            return button;
        }

        QWidget* row(std::initializer_list<QWidget*> widgets, int spacing = 8) {
            auto* wrap = new QWidget;
            auto* h = new QHBoxLayout(wrap);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(spacing);
            for (QWidget* w : widgets) h->addWidget(w, 0, Qt::AlignVCenter);
            h->addStretch();
            return wrap;
        }
    }

    ProjectSettingsDialog::ProjectSettingsDialog(const ProjectSettings& current, const ProjectSettings& app_defaults,
        const QList<QColor>& recent_colours, QWidget* parent)
        : QDialog(parent)
        , original_(current.clamped())
        , app_defaults_(app_defaults.clamped())
    {
        setWindowTitle(QStringLiteral("Ajustes del proyecto"));
        setObjectName("NodeDialog");
        setModal(true);
        // As the node dialogs: no system title bar, a rounded card on a transparent window.
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet(dialog_theme::styleSheet());
        const qreal dpr = devicePixelRatioF();

        auto* banner = new DialogBanner(DialogBanner::Glyph::Settings, QStringLiteral("Ajustes del proyecto"),
            QStringLiteral("Lo que este proyecto decide para todos sus esquemas"), this);
        connect(banner, &DialogBanner::closeRequested, this, &QDialog::reject);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* frame = new QFrame(this);
        frame->setObjectName("nodeDialogCard");
        outer->addWidget(frame);
        auto* root = new QVBoxLayout(frame);
        root->setContentsMargins(1, 1, 1, 1);
        root->setSpacing(0);
        root->addWidget(banner);

        // ── Body ──────────────────────────────────────────────────────────────────
        auto* body_widget = new QWidget;
        body_widget->setObjectName("dialogBody");
        auto* body = new QVBoxLayout(body_widget);
        body->setContentsMargins(22, 16, 22, 12);
        body->setSpacing(12);

        auto* card = new QFrame;
        card->setObjectName("card");
        body->addWidget(card);
        auto* grid = new QGridLayout(card);
        grid->setContentsMargins(20, 14, 20, 18);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(8);
        grid->setColumnStretch(1, 1);
        int r = 0;
        auto addSection = [&](const QString& title, const QString& note) {
            grid->addWidget(sectionTitle(title), r++, 0, 1, 2);
            if (!note.isEmpty()) grid->addWidget(hint(note), r++, 0, 1, 2);
        };
        auto addField = [&](const QString& label, QWidget* editor) {
            grid->addWidget(fieldLabel(label), r, 0, Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(editor, r++, 1);
        };

        // Cajas nuevas.
        addSection(QStringLiteral("Valores iniciales de las cajas nuevas"),
            QStringLiteral("Cada caja nueva empieza así, y después puedes cambiarla desde sus propiedades. "));
        shape_ = new QListWidget;
        shape_->setObjectName("shapeList");
        shape_->setViewMode(QListView::IconMode);
        shape_->setFlow(QListView::LeftToRight);
        shape_->setWrapping(false);
        shape_->setMovement(QListView::Static);
        shape_->setResizeMode(QListView::Adjust);
        shape_->setIconSize(QSize(46, 34));
        shape_->setGridSize(QSize(98, 72));
        shape_->setFixedHeight(80);
        shape_->setMinimumWidth(4 * 98 + 8);
        shape_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        shape_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        shape_->setCursor(Qt::PointingHandCursor);
        for (NodeShape s : { NodeShape::Rectangle, NodeShape::RoundedRectangle, NodeShape::Circle, NodeShape::Rhombus }) {
            auto* item = new QListWidgetItem(QIcon(shapeIcon(s, QSize(46, 34), dpr)), shapeName(s));
            item->setData(Qt::UserRole, static_cast<int>(s));
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            item->setSizeHint(QSize(92, 66));
            shape_->addItem(item);
        }
        connect(shape_, &QListWidget::currentRowChanged, this, [this](int row) {
            if (syncing_ || row < 0) return;
            settings_.shape = static_cast<NodeShape>(shape_->item(row)->data(Qt::UserRole).toInt());
            sync();
        });
        addField(QStringLiteral("Forma por defecto"), shape_);

        colour_ = new ColorPickerButton;
        colour_->setRecentColours(recent_colours);
        colour_->setFixedWidth(196);
        connect(colour_, &ColorPickerButton::colourPicked, this, [this](const QColor& c) {
            settings_.colour = nv::fromQColor(c);
            if (font_auto_) settings_.font_colour = nv::fromQColor(nv::contrastingTextColour(c));
            sync();
        });
        addField(QStringLiteral("Color de fondo por defecto"), row({ colour_ }));

        font_colour_ = new ColorPickerButton;
        font_colour_->setRecentColours(recent_colours);
        font_colour_->setAutomaticAvailable(true);
        font_colour_->setFixedWidth(196);
        connect(font_colour_, &ColorPickerButton::colourPicked, this, [this](const QColor& c) {
            font_auto_ = false;
            settings_.font_colour = nv::fromQColor(c);
            sync();
        });
        connect(font_colour_, &ColorPickerButton::automaticPicked, this, [this] {
            font_auto_ = true;
            settings_.font_colour = nv::fromQColor(nv::contrastingTextColour(nv::toQColor(settings_.colour)));
            sync();
        });
        addField(QStringLiteral("Color del texto por defecto"), row({ font_colour_ }));

        font_size_ = new FontSizeSelector;
        font_size_->setFixedWidth(196);
        connect(font_size_, &FontSizeSelector::valueChanged, this, [this](int v) {
            if (syncing_) return;
            settings_.font_size = v;
            sync();
        });
        addField(QStringLiteral("Tamaño de fuente por defecto"), row({ font_size_ }));

        preview_ = new NodePreview;
        preview_->setFixedSize(300, 120);
        addField(QStringLiteral("Así empiezan"), row({ preview_ }));

        // Equivalencias nuevas.
        addSection(QStringLiteral("Equivalencias nuevas"),
            QStringLiteral("Cada equivalencia puede cambiar el tipo de línea después."));
        continuous_ = new TickBox(QStringLiteral("Línea continua"), style::icon(style::Icon::EquivalenceSolid));
        dashed_ = new TickBox(QStringLiteral("Línea discontinua"), style::icon(style::Icon::EquivalenceDashed));
        continuous_->setToolTip(QStringLiteral("Las equivalencias nuevas empiezan siendo seguras"));
        dashed_->setToolTip(QStringLiteral("Las equivalencias nuevas empiezan siendo hipotéticas"));
        // One of the two, always.
        connect(continuous_, &TickBox::toggled, this, [this](bool on) {
            if (syncing_) return;
            settings_.equivalence_continuous = on;
            sync();
        });
        connect(dashed_, &TickBox::toggled, this, [this](bool on) {
            if (syncing_) return;
            settings_.equivalence_continuous = !on;
            sync();
        });
        addField(QStringLiteral("Por defecto"), row({ continuous_, dashed_ }, 18));

        // Distancias.
        addSection(QStringLiteral("Distancias"), QStringLiteral("Para todos los esquemas del proyecto, que se redibujan con ellas, como el ejemplo de abajo."));
        // Each one dragged along its slider, or typed in its field.
        auto addLength = [&](const QString& label, QSpinBox*& spin, DistanceSlider*& slider,
                             double min, double max, double LayoutSettings::* field) {
            spin = lengthField(min, max, label);
            slider = new DistanceSlider(static_cast<int>(min), static_cast<int>(max));
            slider->setFixedWidth(250);
            slider->setToolTip(spin->toolTip());
            auto set = [this, field](int v) {
                if (syncing_) return;
                settings_.layout.*field = v;
                sync();
            };
            connect(spin, &QSpinBox::valueChanged, this, set);
            connect(slider, &DistanceSlider::moved, this, set);
            // While dragged, the sample keeps its scale: the distance is seen to grow or shrink.
            connect(slider, &DistanceSlider::draggingChanged, this, [this](bool dragging) {
                layout_preview_->holdScale(dragging);
                showActiveDistance();
            });
            spin->installEventFilter(this);
            slider->installEventFilter(this);
            QToolButton* revert = revertButton();
            connect(revert, &QToolButton::clicked, this, [this, field] {
                settings_.layout.*field = original_.layout.*field;
                sync();
            });
            reverts_.push_back({ revert, [this, field] {
                const double was = original_.layout.*field;
                return std::pair{ settings_.layout.*field != was, QStringLiteral("%1 px").arg(qRound(was)) };
            } });
            addField(label, row({ slider, spin, unit(QStringLiteral("px")), revert }, 10));
        };
        addLength(QStringLiteral("Entre cajas vecinas"), block_sep_, block_slider_,
            LayoutSettings::MIN_BLOCK_SEP_SETTING, LayoutSettings::MAX_BLOCK_SEP_SETTING, &LayoutSettings::block_sep);
        addLength(QStringLiteral("Entre cajas y conexiones"), layer_gap_, layer_slider_,
            LayoutSettings::MIN_LAYER_GAP_SETTING, LayoutSettings::MAX_LAYER_GAP_SETTING, &LayoutSettings::layer_gap);
        addLength(QStringLiteral("Entre líneas horizontales"), horizontal_sep_, horizontal_slider_,
            LayoutSettings::MIN_HORIZONTAL_SEP_SETTING, LayoutSettings::MAX_HORIZONTAL_SEP_SETTING, &LayoutSettings::horizontal_sep);
        layout_preview_ = new LayoutPreview;
        layout_preview_->setFixedHeight(230);
        grid->addWidget(layout_preview_, r++, 0, 1, 2);
        // A mark clicked on the sample: its field, ready to type in.
        connect(layout_preview_, &LayoutPreview::distancePicked, this, [this](LayoutPreview::Distance d) {
            QSpinBox* spin = d == LayoutPreview::Distance::BlockSep ? block_sep_
                : d == LayoutPreview::Distance::LayerGap ? layer_gap_ : horizontal_sep_;
            spin->setFocus(Qt::MouseFocusReason);
            spin->selectAll();
        });

        // Tamaño por defecto.
        addSection(QStringLiteral("Tamaño por defecto de las cajas"),
            QStringLiteral("Para aquellas cajas que usaban el anterior tamaño por defecto, "
                            "al presionar Aceptar se pregunta si deben ajustarse al nuevo estándar."));
        // The three side by side, to compare them and drag their edges.
        sizes_preview_ = new ShapeSizesPreview;
        sizes_preview_->setFixedHeight(230);
        grid->addWidget(sizes_preview_, r++, 0, 1, 2);
        connect(sizes_preview_, &ShapeSizesPreview::sizeEdited, this, [this](NodeShape shape, double w, double h) {
            BoxSize& size = shape == NodeShape::Circle ? settings_.layout.box_sizes.circle
                : shape == NodeShape::Rhombus ? settings_.layout.box_sizes.rhombus : settings_.layout.box_sizes.rectangle;
            size = BoxSize{ w, h };
            sync();
        });
        auto addSize = [&](const QString& label, NodeShape shape, SizeFields& fields, BoxSize BoxSizes::* box) {
            fields.width = lengthField(MIN_NODE_WIDTH, MAX_NODE_WIDTH, QStringLiteral("Ancho"));
            fields.height = lengthField(MIN_NODE_HEIGHT, MAX_NODE_HEIGHT, QStringLiteral("Alto"));
            connect(fields.width, &QSpinBox::valueChanged, this, [this, box](int v) {
                if (syncing_) return;
                (settings_.layout.box_sizes.*box).width = v;
                sync();
            });
            connect(fields.height, &QSpinBox::valueChanged, this, [this, box](int v) {
                if (syncing_) return;
                (settings_.layout.box_sizes.*box).height = v;
                sync();
            });
            auto* icon = new QLabel;
            icon->setPixmap(shapeIcon(shape, QSize(30, 22), devicePixelRatioF()));
            QToolButton* revert = revertButton();
            connect(revert, &QToolButton::clicked, this, [this, box] {
                settings_.layout.box_sizes.*box = original_.layout.box_sizes.*box;
                sync();
            });
            reverts_.push_back({ revert, [this, box] {
                const BoxSize& was = original_.layout.box_sizes.*box;
                return std::pair{ settings_.layout.box_sizes.*box != was,
                    QStringLiteral("%1 × %2 px").arg(qRound(was.width)).arg(qRound(was.height)) };
            } });
            addField(label, row({ icon, fields.width, unit(QStringLiteral("×")), fields.height, unit(QStringLiteral("px")), revert }));
        };
        addSize(QStringLiteral("Rectángulo y redondeado"), NodeShape::Rectangle, rectangle_, &BoxSizes::rectangle);
        addSize(QStringLiteral("Círculo"), NodeShape::Circle, circle_, &BoxSizes::circle);
        addSize(QStringLiteral("Rombo"), NodeShape::Rhombus, rhombus_, &BoxSizes::rhombus);

        // The application's defaults.
        auto* defaults_card = new QFrame;
        defaults_card->setObjectName("card");
        body->addWidget(defaults_card);
        auto* defaults = new QVBoxLayout(defaults_card);
        defaults->setContentsMargins(20, 14, 20, 16);
        defaults->setSpacing(8);
        defaults->addWidget(sectionTitle(QStringLiteral("Predeterminados de la aplicación")));
        as_app_defaults_ = new TickBox(QStringLiteral("Usar estos ajustes para proyectos futuros"));
        as_app_defaults_->setToolTip(QStringLiteral(
            "Se guardan en la aplicación: todo proyecto nuevo empezará con ellos"));
        defaults->addWidget(as_app_defaults_);
        auto* load_app = new QPushButton(QStringLiteral("Cargar los de la aplicación"));
        load_app->setObjectName("ghost");
        load_app->setCursor(Qt::PointingHandCursor);
        load_app->setToolTip(QStringLiteral("Rellena el formulario con los ajustes con los que empiezan los proyectos nuevos"));
        connect(load_app, &QPushButton::clicked, this, [this] { setSettings(app_defaults_); });
        auto* factory = new QPushButton(QStringLiteral("Valores de fábrica"));
        factory->setObjectName("ghost");
        factory->setCursor(Qt::PointingHandCursor);
        factory->setToolTip(QStringLiteral("Rellena el formulario con los ajustes que trae la aplicación"));
        connect(factory, &QPushButton::clicked, this, [this] { setSettings(ProjectSettings()); });
        defaults->addWidget(row({ load_app, factory }, 10));

        auto* scroll = new QScrollArea;
        scroll->setObjectName("dialogScroll");
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(body_widget);
        scroll->viewport()->setAutoFillBackground(false);
        body_widget->setAutoFillBackground(false);
        root->addWidget(scroll, 1);

        // ── Footer ────────────────────────────────────────────────────────────────
        auto* footer = new QFrame;
        footer->setObjectName("dialogFooter");
        auto* buttons = new QHBoxLayout(footer);
        buttons->setContentsMargins(22, 12, 22, 16);
        buttons->setSpacing(10);
        auto* accept = new QPushButton(QStringLiteral("Aceptar"));
        accept->setObjectName("primary");
        accept->setDefault(true);
        accept->setCursor(Qt::PointingHandCursor);
        auto* cancel = new QPushButton(QStringLiteral("Cancelar"));
        cancel->setObjectName("secondary");
        cancel->setCursor(Qt::PointingHandCursor);
        connect(accept, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        buttons->addWidget(accept);
        buttons->addWidget(cancel);
        buttons->addSpacing(8);
        buttons->addWidget(hint(QStringLiteral("Se aplican a todo el proyecto y no se pueden deshacer.")), 1);
        root->addWidget(footer);
        auto divider = [footer, scroll] {
            footer->setProperty("divided", scroll->verticalScrollBar()->maximum() > 0);
            dialog_theme::repolish(footer);
        };
        connect(scroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, divider);

        // Enter in a field keeps what was typed there (see eventFilter), and
        // otherwise only "Aceptar" answers it: never a button that refills the form.
        for (QAbstractSpinBox* field : findChildren<QAbstractSpinBox*>()) field->installEventFilter(this);
        for (QPushButton* button : findChildren<QPushButton*>())
            if (button != accept) button->setAutoDefault(false);

        setSettings(current);
        dialog_theme::fitToWindow(this, banner, scroll, footer, 640);
        divider();
    }

    bool ProjectSettingsDialog::saveAsAppDefaults() const {
        return as_app_defaults_->isChecked();
    }

    void ProjectSettingsDialog::setSettings(const ProjectSettings& settings) {
        settings_ = settings.clamped();
        font_auto_ = nv::toQColor(settings_.font_colour).rgb()
            == nv::contrastingTextColour(nv::toQColor(settings_.colour)).rgb();
        sync();
    }

    void ProjectSettingsDialog::sync() {
        syncing_ = true;
        for (int i = 0; i < shape_->count(); ++i)
            if (shape_->item(i)->data(Qt::UserRole).toInt() == static_cast<int>(settings_.shape)) shape_->setCurrentRow(i);
        colour_->setColour(nv::toQColor(settings_.colour), false);
        font_colour_->setColour(nv::toQColor(settings_.font_colour), font_auto_);
        font_size_->setValue(settings_.font_size);

        NodeAttributes box(std::string("Caja nueva"));
        box.shape = settings_.shape;
        box.colour = settings_.colour;
        box.font_colour = settings_.font_colour;
        box.font_size = settings_.font_size;
        preview_->setBoxSizes(settings_.layout.box_sizes);
        preview_->setAttributes(box);

        continuous_->setChecked(settings_.equivalence_continuous);
        dashed_->setChecked(!settings_.equivalence_continuous);

        block_sep_->setValue(qRound(settings_.layout.block_sep));
        layer_gap_->setValue(qRound(settings_.layout.layer_gap));
        horizontal_sep_->setValue(qRound(settings_.layout.horizontal_sep));
        const std::pair<SizeFields*, const BoxSize*> sizes[] = {
            { &rectangle_, &settings_.layout.box_sizes.rectangle },
            { &circle_, &settings_.layout.box_sizes.circle },
            { &rhombus_, &settings_.layout.box_sizes.rhombus },
        };
        for (const auto& [fields, size] : sizes) {
            fields->width->setValue(qRound(size->width));
            fields->height->setValue(qRound(size->height));
        }
        for (auto [slider, v] : { std::pair{ block_slider_, settings_.layout.block_sep },
                                  std::pair{ layer_slider_, settings_.layout.layer_gap },
                                  std::pair{ horizontal_slider_, settings_.layout.horizontal_sep } })
            slider->setValue(qRound(v));
        // At once while a slider is dragged, so the sample follows it.
        const bool dragging = block_slider_->isDragging() || layer_slider_->isDragging() || horizontal_slider_->isDragging();
        layout_preview_->setSettings(settings_.layout, !dragging);
        sizes_preview_->setSizes(settings_.layout.box_sizes);
        for (const Revert& revert : reverts_) {
            const auto [changed, was] = revert.state();
            revert.button->setEnabled(changed);
            revert.button->setToolTip(QStringLiteral("Volver al valor del proyecto (%1)").arg(was));
        }
        syncing_ = false;
    }

    bool ProjectSettingsDialog::eventFilter(QObject* watched, QEvent* event) {
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (auto* field = qobject_cast<QAbstractSpinBox*>(watched); field && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
                // The value typed is taken, and stays: the window neither closes nor
                // passes the key on to a button.
                field->interpretText();
                field->selectAll();
                return true;
            }
        }
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::Leave:
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            // Once the field knows whether it has the mouse or the focus.
            QTimer::singleShot(0, this, &ProjectSettingsDialog::showActiveDistance);
            break;
        default:
            break;
        }
        return QDialog::eventFilter(watched, event);
    }

    void ProjectSettingsDialog::showActiveDistance() {
        struct Field { QSpinBox* spin; DistanceSlider* slider; LayoutPreview::Distance distance; };
        const Field fields[] = {
            { block_sep_, block_slider_, LayoutPreview::Distance::BlockSep },
            { layer_gap_, layer_slider_, LayoutPreview::Distance::LayerGap },
            { horizontal_sep_, horizontal_slider_, LayoutPreview::Distance::HorizontalSep },
        };
        std::optional<LayoutPreview::Distance> active;
        for (const Field& f : fields) if (f.spin->hasFocus() || f.slider->hasFocus()) active = f.distance;
        for (const Field& f : fields) if (f.spin->underMouse() || f.slider->underMouse()) active = f.distance; // the mouse wins
        for (const Field& f : fields) if (f.slider->isDragging()) active = f.distance; // and a drag, wherever the mouse is
        layout_preview_->setActive(active);
    }

    // ============================================================================
    // The question about existing boxes
    // ============================================================================

    StyledDialog* existingBoxesQuestion(QWidget* parent) {
        auto* ask = new StyledDialog(StyledDialog::Badge::Question, QStringLiteral("¿Y las cajas que ya existen?"), parent);
        ask->setMessage(QStringLiteral(
            "Has cambiado el tamaño por defecto de alguna forma, y hay cajas que lo usan porque no tienen "
            "un tamaño propio. ¿Deben tomar el tamaño nuevo, o quedarse con el que tienen?"));
        ask->addButton(QStringLiteral("Cancelar"), -1, StyledDialog::ButtonStyle::Secondary, false, true);
        ask->addButton(QStringLiteral("Solo las cajas nuevas"), 0, StyledDialog::ButtonStyle::Secondary);
        ask->addButton(QStringLiteral("También las existentes"), 1, StyledDialog::ButtonStyle::Primary, true);
        return ask;
    }

    std::optional<app_logic::Project::ExistingBoxes> askAboutExistingBoxes(QWidget* parent) {
        StyledDialog* ask = existingBoxesQuestion(parent);
        ask->exec();
        const int choice = ask->choice();
        delete ask;
        if (choice == 0) return app_logic::Project::ExistingBoxes::KeepTheirSize;
        if (choice == 1) return app_logic::Project::ExistingBoxes::TakeNewSize;
        return std::nullopt;
    }

} // namespace ui
