#include "Tutorial.h"
#include "AppDialogs.h"
#include "Project.h"
#include "UiStyle.h"

#include <QApplication>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace ui::tutorial {

    namespace {

        // ============================================================================
        // Content
        // ============================================================================

        // A key or a mouse gesture, set off from the text.
        QString key(const QString& k) {
            return QStringLiteral("<span style=\"background-color:#EEF0FF; color:#4F46E5;\">&nbsp;<b>%1</b>&nbsp;</span>").arg(k);
        }

        QString picture(const char* name) {
            return QStringLiteral(":/tutorial/%1.png").arg(QLatin1String(name));
        }

        const QString SEEN_KEY = QStringLiteral("tutorial/seen");

        constexpr int CARD_WIDTH = 470;
        constexpr int CARD_PADDING = 22;

    } // namespace

    std::vector<Step> introSteps() {
        using T = Target;
        return {
            { QStringLiteral("¡Te damos la bienvenida al Taller Matrix Harris!"),
              QStringLiteral("Aquí dibujas esquemas Matrix Harris y cualquier tipo de esquema jerárquico: <b>cajas</b> " 
                             "colocadas en <b>niveles</b> y unidas "
                             "por <b>conexiones</b>. En un par de minutos verás lo esencial.<br><br>"
                             "Para que pruebes cada paso, hemos abierto un <b>proyecto de ejemplo</b>: la parte "
                             "resaltada funciona de verdad. Lo que hagas en él es solo para practicar y desaparece "
                             "al terminar.<br><br>"
                             "Puedes saltar el recorrido cuando quieras y repetirlo más tarde desde <b>Ayuda</b>."),
              picture("welcome"), {} },

            { QStringLiteral("El lienzo"),
              QStringLiteral("Aquí se dibuja el esquema que tienes abierto. Para crear una caja suelta, haz %1 "
                             "en el fondo y elige <b>Nueva caja</b>. Pruébalo.").arg(key(QStringLiteral("clic derecho"))),
              picture("new_box"), { T::Canvas }, View::FirstDiagram },

            { QStringLiteral("Todo empieza con un clic derecho"),
              QStringLiteral("Haz %1 sobre una caja para crear otras arriba, abajo o a sus lados, conectarla con "
                             "otra, fusionarlas, cambiar sus conexiones o eliminarla. Prueba a crear una caja "
                             "debajo de la 4.").arg(key(QStringLiteral("clic derecho"))),
              picture("box_menu"), { T::Canvas }, View::FirstDiagram },

            { QStringLiteral("Operaciones con dos cajas"),
              QStringLiteral("Algunas opciones, como <b>Caja actual por debajo de</b>, te piden elegir otra caja: "
                             "las que sirven quedan resaltadas y el resto se apaga. Haz clic en una, o pulsa %1 "
                             "para cancelar.").arg(key(QStringLiteral("Esc"))),
              picture("pick_second"), { T::Canvas }, View::FirstDiagram },

            { QStringLiteral("Las conexiones también tienen menú"),
              QStringLiteral("Haz %1 sobre una línea para poner una caja en medio, bifurcarla, dibujarla discontinua "
                             "o eliminarla.").arg(key(QStringLiteral("clic"))),
              picture("edge_menu"), { T::Canvas }, View::FirstDiagram },

            { QStringLiteral("Cambia el aspecto de una caja"),
              QStringLiteral("Haz %1 sobre una caja para editar su nombre, su forma, sus colores y el tamaño de la "
                             "letra. Si el nombre no cabe, la rueda del ratón sobre la caja lo desplaza.")
                  .arg(key(QStringLiteral("doble clic"))),
              picture("properties"), { T::Canvas }, View::FirstDiagram },

            { QStringLiteral("Mueve las cajas"),
              QStringLiteral("%1 una caja para cambiarla de sitio dentro de su nivel o llevarla a otro: las guías te "
                             "muestran dónde caerá. Si la sueltas más allá del primer o del último nivel, se abre "
                             "uno nuevo. Prueba a llevar la 6 junto a la 4.").arg(key(QStringLiteral("Arrastra"))),
              picture("drag_box"), { T::Canvas }, View::FirstDiagram },

            { QStringLiteral("Mueve bloques enteros"),
              QStringLiteral("Con clic derecho sobre una caja › <b>Mover bloque</b> te llevas todo su grupo de cajas "
                             "conectadas colgando del ratón: haz clic donde quieras dejarlo (clic derecho lo devuelve "
                             "a su sitio). <b>Eliminar bloque</b> lo borra entero. Tiene que haber <b>varias</b> piezas "
                             "para que se muestre esta opción."),
              picture("move_block"), { T::Canvas }, View::FirstDiagram },

            { QStringLiteral("Muévete por el esquema"),
              QStringLiteral("%1 para desplazarte y usa %2 para acercar o alejar. %3 ajusta todo el esquema a la "
                             "ventana. Abajo a la derecha ves el zoom actual.")
                  .arg(key(QStringLiteral("Arrastra el fondo")), key(QStringLiteral("Ctrl + rueda")),
                      key(QStringLiteral("Ctrl+0"))),
              {}, { T::Canvas, T::ZoomLabel }, View::FirstDiagram },

            { QStringLiteral("Un proyecto, varios esquemas"),
              QStringLiteral("Cada esquema del proyecto tiene aquí su pestaña, con una miniatura. Haz %1 "
                             "fuera de la miniatura para abrirla. %2 pasa a la siguiente. Prueba a abrir el «Esquema 2».")
                  .arg(key(QStringLiteral("clic en una pestaña")))
                  .arg(key(QStringLiteral("Ctrl+Tab"))),
              {}, { T::TabStrip } },

            { QStringLiteral("Las miniaturas también se exploran"),
              QStringLiteral("Sobre una miniatura, %1 la acerca o la aleja y %2 la desplaza, sin cambiar de esquema.")
                  .arg(key(QStringLiteral("Ctrl + rueda")), key(QStringLiteral("arrastrar"))),
              {}, { T::FirstTabMiniature } },

            { QStringLiteral("Renombra, duplica o elimina un esquema"),
              QStringLiteral("Haz %1 sobre el nombre para cambiarlo (o pulsa %2). Al pasar el ratón por la pestaña "
                             "aparece una papelera para eliminar el esquema. Haciendo %3 se accede a las dos opciones"
                             "anteriores y a la de duplicar.")
                  .arg(key(QStringLiteral("doble clic")), key(QStringLiteral("F2")), key(QStringLiteral("clic derecho"))),
              {}, { T::FirstTabName } },

            { QStringLiteral("Nuevo esquema"),
              QStringLiteral("Este botón añade un esquema vacío al proyecto (también con %1).")
                  .arg(key(QStringLiteral("Ctrl+T"))),
              {}, { T::AddTab } },

            { QStringLiteral("El esquema conjunto"),
              QStringLiteral("Reúne varios esquemas, uno al lado de otro, para relacionarlos: en él puedes conectar o "
                             "fusionar cajas de esquemas distintos. Cada esquema entra como una copia, así que los "
                             "cambios que hagas aquí no afectan a los originales. Ábrelo con %1 o haciendo %2 fuera de la miniatura.")
                    .arg(key(QStringLiteral("Ctrl+J")))
                    .arg(key(QStringLiteral("clic en su pestaña"))),
              {}, { T::JointTab } },

            { QStringLiteral("Administra el esquema conjunto"),
              QStringLiteral("En el esquema conjunto, haz clic derecho en el fondo › <b>Administrar esquemas</b>: "
                             "con <b>+</b> añades un esquema y con la papelera lo quitas. Sobre una caja, "
                             "<b>Mover bloque</b>, <b>Mover esquema</b> y <b>Quitar bloque</b> lo recolocan o sacan sus piezas. Pruébalo: "
                             "añade el «Esquema 2»."),
              picture("joint_manage"), { T::JointTab, T::Canvas }, View::Joint },

            { QStringLiteral("Menos cruces, más claridad"),
              QStringLiteral("<b>Minimizar cruces</b> reordena las cajas para que las líneas se crucen lo menos "
                             "posible. El rayo es el modo rápido; la tortuga, uno más lento y exhaustivo que puedes "
                             "pausar. Arrastra el panel por su asa para apartarlo."),
              {}, { T::MinimizePanel } },

            { QStringLiteral("Deshaz, guarda y comparte"),
              QStringLiteral("Cualquier cambio se deshace con %1 y se rehace con %2 (menú <b>Editar</b>). Guarda el "
                             "proyecto con %3 y exporta tus esquemas como PDF o imágenes con %4 (menú <b>Archivo</b>).")
                  .arg(key(QStringLiteral("Ctrl+Z")), key(QStringLiteral("Ctrl+Y")),
                      key(QStringLiteral("Ctrl+S")), key(QStringLiteral("Ctrl+E"))),
              {}, { T::FileMenu, T::EditMenu } },

            { QStringLiteral("¡Listo para empezar!"),
              QStringLiteral("En <b>Ayuda</b> tienes todos los atajos de teclado y puedes repetir este recorrido "
                             "cuando quieras. Al terminar se cierra el proyecto de ejemplo y vuelves a tu proyecto "
                             "(o a uno nuevo)."),
              {}, { T::HelpMenu } },
        };
    }

    std::unique_ptr<app_logic::Project> exampleProject() {
        using namespace app_logic;
        using namespace hypergraph_logic;
        QTemporaryDir dir;
        const std::filesystem::path file = dir.filePath(QStringLiteral("ejemplo.json")).toStdWString();
        {
            // Built, then saved and loaded back: what building it did is then
            // not in its history (nothing to undo, nothing unsaved).
            Project project("Proyecto de ejemplo");
            {
                auto& ed = project.getEditor(0);
                ed.setName("Esquema 1");
                auto n1 = ed.createNode(NodeAttributes("1"), 0, -1, nullptr);
                auto n2 = ed.createNode(NodeAttributes("2"), 1, -1, n1);
                auto n3 = ed.createNode(NodeAttributes("3"), 1, -1, n1);
                ed.createNode(NodeAttributes("4"), 2, -1, n2);
                auto n5 = ed.createNode(NodeAttributes("5"), 2, -1, n2);
                ed.createNode(NodeAttributes("6"), 2, -1, n3);
                ed.addConnection(n3, n5);
            }
            project.addDiagram();
            {
                auto& ed = project.getEditor(1);
                ed.setName("Esquema 2");
                auto n7 = ed.createNode(NodeAttributes("7"), 0, -1, nullptr);
                ed.createNode(NodeAttributes("8"), 1, -1, n7);
                ed.createNode(NodeAttributes("9"), 1, -1, n7);
            }
            // Only the first one in the joint: adding the second is a step.
            project.getJointEditor().addHypergraph(
                const_cast<GraphicalHypergraph&>(project.getEditor(0).getGraph()), 0.0);
            project.setActive(0);
            project.save(file);
        }
        std::unique_ptr<Project> example = Project::load(file);
        example->detachFromFile();
        return example;
    }

    bool pendingAtStartup(QSettings& settings) {
        return !settings.value(SEEN_KEY, false).toBool();
    }

    void markSeen(QSettings& settings) {
        settings.setValue(SEEN_KEY, true);
    }

    // ============================================================================
    // TutorialOverlay
    // ============================================================================

    namespace {

        // Thin progress line along the top of the card.
        class Progress : public QWidget {
        public:
            explicit Progress(QWidget* parent) : QWidget(parent) { setFixedHeight(4); }
            void setFraction(double f) { fraction_ = f; update(); }

        protected:
            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0xEE, 0xF0, 0xF6));
                p.drawRoundedRect(QRectF(rect()), 2, 2);
                QRectF done(0, 0, width() * fraction_, height());
                QLinearGradient g(done.topLeft(), done.topRight());
                g.setColorAt(0, style::palette::accent);
                g.setColorAt(1, style::palette::violet);
                p.setBrush(g);
                p.drawRoundedRect(done, 2, 2);
            }

        private:
            double fraction_ = 0.0;
        };

        QRectF lerp(const QRectF& a, const QRectF& b, double t) {
            return QRectF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t,
                a.width() + (b.width() - a.width()) * t, a.height() + (b.height() - a.height()) * t);
        }

        QRectF unite(const std::vector<QRectF>& rects) {
            QRectF u;
            for (const QRectF& r : rects) u = u.isNull() ? r : u.united(r);
            return u;
        }

        // The picture with rounded corners and a hairline border, fitted into
        // width x max_height.
        QPixmap roundedPicture(const QPixmap& source, int width, int max_height, qreal dpr) {
            QSize size = source.size().scaled(width, max_height, Qt::KeepAspectRatio);
            QPixmap out(size * dpr);
            out.setDevicePixelRatio(dpr);
            out.fill(Qt::transparent);
            QPainter p(&out);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            const QRectF r(0.5, 0.5, size.width() - 1.0, size.height() - 1.0);
            QPainterPath clip;
            clip.addRoundedRect(r, 10, 10);
            p.setClipPath(clip);
            p.drawPixmap(QRectF(0, 0, size.width(), size.height()),
                source.scaled(size * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation), QRectF());
            p.setClipping(false);
            p.setPen(QPen(style::palette::line, 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 10, 10);
            return out;
        }

    } // namespace

    TutorialOverlay::TutorialOverlay(QWidget* window, std::vector<Step> steps, Locator locate, Shower show)
        : QWidget(window), steps_(std::move(steps)), locate_(std::move(locate)), show_(std::move(show))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setFocusPolicy(Qt::StrongFocus);
        setGeometry(window->rect());
        window->installEventFilter(this);

        morph_anim_ = new QVariantAnimation(this);
        morph_anim_->setStartValue(0.0);
        morph_anim_->setEndValue(1.0);
        morph_anim_->setDuration(320);
        morph_anim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(morph_anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            morph_ = v.toDouble();
            update();
        });

        follow_timer_.setInterval(250);
        connect(&follow_timer_, &QTimer::timeout, this, [this] { follow(); });

        // ── The card ──────────────────────────────────────────────────────────
        card_ = new QFrame(this);
        card_->setObjectName("tutorialCard");
        card_->setFixedWidth(CARD_WIDTH);
        card_->setStyleSheet(dialogs::styleSheet() + QStringLiteral(R"(
QFrame#tutorialCard { background: white; border: 1px solid #E3E6EF; border-radius: 16px; }
QLabel { background: transparent; border: none; }
QLabel#counter { color: #6366F1; font-size: 8.5pt; font-weight: 700; }
QLabel#title { color: #1F2330; font-size: 13pt; font-weight: 700; }
QLabel#text { color: #4B5068; font-size: 10.5pt; }
QPushButton#skip { background: transparent; border: none; color: #8A90A2; font-weight: 600; padding: 2px 4px; }
QPushButton#skip:hover { color: #4F46E5; }
)"));
        auto* shadow = new QGraphicsDropShadowEffect(card_);
        shadow->setBlurRadius(40);
        shadow->setOffset(0, 10);
        shadow->setColor(QColor(10, 12, 24, 110));
        card_->setGraphicsEffect(shadow);

        auto* col = new QVBoxLayout(card_);
        col->setContentsMargins(CARD_PADDING, 18, CARD_PADDING, 18);
        col->setSpacing(10);

        auto* bar = new Progress(card_);
        progress_ = bar;
        col->addWidget(progress_);

        auto* head = new QHBoxLayout;
        counter_ = new QLabel(card_);
        counter_->setObjectName("counter");
        head->addWidget(counter_);
        head->addStretch();
        auto* skip = new QPushButton(QStringLiteral("Saltar tutorial"), card_);
        skip->setObjectName("skip");
        skip->setCursor(Qt::PointingHandCursor);
        skip->setFocusPolicy(Qt::NoFocus);
        connect(skip, &QPushButton::clicked, this, [this] { finish(false); });
        head->addWidget(skip);
        col->addLayout(head);

        title_ = new QLabel(card_);
        title_->setObjectName("title");
        title_->setWordWrap(true);
        col->addWidget(title_);

        text_ = new QLabel(card_);
        text_->setObjectName("text");
        text_->setWordWrap(true);
        text_->setTextFormat(Qt::RichText);
        col->addWidget(text_);

        picture_ = new QLabel(card_);
        picture_->setAlignment(Qt::AlignCenter);
        col->addSpacing(2);
        col->addWidget(picture_);

        auto* buttons = new QHBoxLayout;
        buttons->setSpacing(8);
        buttons->addStretch();
        prev_ = new QPushButton(QStringLiteral("‹  Anterior"), card_);
        prev_->setObjectName("secondary");
        next_ = new QPushButton(card_);
        next_->setObjectName("primary");
        for (QPushButton* b : { prev_, next_ }) {
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus); // the keys stay with the overlay
            buttons->addWidget(b);
        }
        prev_->setStyleSheet(QStringLiteral(
            "QPushButton:disabled { color: #B8BDCB; border-color: #E8EAF0; background: white; }"));
        connect(prev_, &QPushButton::clicked, this, [this] { go(current_ - 1); });
        connect(next_, &QPushButton::clicked, this, [this] {
            if (current_ + 1 < static_cast<int>(steps_.size())) go(current_ + 1);
            else finish(true);
        });
        col->addSpacing(4);
        col->addLayout(buttons);

        // The card always takes the mouse, wherever it sits.
        card_->installEventFilter(this);
    }

    void TutorialOverlay::start() {
        show();
        raise();
        setFocus();
        go(0);
        follow_timer_.start();
    }

    void TutorialOverlay::updateMask() {
        QRegion region(rect());
        for (const QRectF& h : to_) region -= QRegion(h.toAlignedRect());
        region += QRegion(card_->geometry().adjusted(-2, -2, 2, 2));
        setMask(region);
    }

    void TutorialOverlay::follow() {
        if (current_ < 0 || morph_ < 1.0) return;
        const std::vector<QRectF> now = holesFor(steps_[current_]);
        bool same = now.size() == to_.size();
        for (size_t i = 0; same && i < now.size(); ++i)
            same = (now[i].topLeft() - to_[i].topLeft()).manhattanLength() < 2
                && std::abs(now[i].width() - to_[i].width()) < 2 && std::abs(now[i].height() - to_[i].height()) < 2;
        if (same) return;
        from_ = to_ = now;
        card_->move(cardPosition(unite(to_).toAlignedRect(), card_->size()));
        updateMask();
        update();
    }

    std::vector<QRectF> TutorialOverlay::holesFor(const Step& step) const {
        std::vector<QRectF> holes;
        const QRectF bounds = QRectF(rect()).adjusted(3, 3, -3, -3);
        for (Target t : step.targets) {
            const QRect r = locate_ ? locate_(t) : QRect();
            if (r.isEmpty()) continue;
            QRectF hole = QRectF(r).adjusted(-6, -6, 6, 6).intersected(bounds);
            // Parts side by side (two menu titles) share one outline.
            for (auto it = holes.begin(); it != holes.end();) {
                if (it->intersects(hole)) {
                    hole = hole.united(*it);
                    it = holes.erase(it);
                }
                else {
                    ++it;
                }
            }
            holes.push_back(hole);
        }
        return holes;
    }

    void TutorialOverlay::go(int index) {
        if (index < 0 || index >= static_cast<int>(steps_.size()) || index == current_) return;
        current_ = index;
        if (show_ && steps_[index].view != View::Keep) show_(steps_[index].view);
        refresh(true);
        // Back to the tour's keys (the user may have been clicking around).
        setFocus();
    }

    void TutorialOverlay::refresh(bool animate) {
        if (current_ < 0) return;
        const Step& step = steps_[current_];
        const int n = static_cast<int>(steps_.size());

        // ── The outline moves from where it was to the new parts ─────────────
        const std::vector<QRectF> now = shownHoles();
        to_ = holesFor(step);
        if (animate) {
            from_ = now;
            morph_ = 0.0;
            morph_anim_->stop();
            morph_anim_->start();
        }
        else {
            from_ = to_;
            morph_ = 1.0;
            update();
        }
        updateMask();

        // ── The card ──────────────────────────────────────────────────────────
        static_cast<Progress*>(progress_)->setFraction((current_ + 1) / static_cast<double>(n));
        counter_->setText(QStringLiteral("%1 de %2").arg(current_ + 1).arg(n));
        title_->setText(step.title);
        text_->setText(step.text);
        const QPixmap source = step.image.isEmpty() ? QPixmap() : QPixmap(step.image);
        picture_->setVisible(!source.isNull());
        if (!source.isNull()) {
            // As large as the window leaves room for (the rest of the card
            // takes about 330 px), so the menus in the pictures stay legible.
            const int max_height = std::clamp(height() - 360, 140, 420);
            picture_->setPixmap(roundedPicture(source, CARD_WIDTH - 2 * CARD_PADDING, max_height, devicePixelRatioF()));
        }
        prev_->setEnabled(current_ > 0);
        next_->setText(current_ + 1 < n ? QStringLiteral("Siguiente  ›") : QStringLiteral("Terminar"));
        // The wrapped labels need their height for the card's width.
        card_->layout()->activate();
        card_->resize(CARD_WIDTH, card_->layout()->totalHeightForWidth(CARD_WIDTH));

        const QPoint target = cardPosition(unite(holesFor(step)).toAlignedRect(), card_->size());
        if (animate && card_placed_) {
            auto* move = new QPropertyAnimation(card_, "pos", card_);
            move->setDuration(320);
            move->setEasingCurve(QEasingCurve::OutCubic);
            move->setEndValue(target);
            move->start(QAbstractAnimation::DeleteWhenStopped);
        }
        else {
            card_->move(target);
            card_->show();
            card_placed_ = true;
        }
    }

    QPoint TutorialOverlay::cardPosition(const QRect& focus, QSize card) const {
        constexpr int MARGIN = 16, GAP = 18;
        const QRect area = rect().adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
        auto clampX = [&](int x) { return std::clamp(x, area.left(), std::max(area.left(), area.right() - card.width())); };
        auto clampY = [&](int y) { return std::clamp(y, area.top(), std::max(area.top(), area.bottom() - card.height())); };

        if (focus.isEmpty())
            return QPoint(clampX(rect().center().x() - card.width() / 2), clampY(rect().center().y() - card.height() / 2));

        // Next to it, where it fits: below, above, right, left.
        if (focus.bottom() + GAP + card.height() <= area.bottom())
            return QPoint(clampX(focus.center().x() - card.width() / 2), focus.bottom() + GAP);
        if (focus.top() - GAP - card.height() >= area.top())
            return QPoint(clampX(focus.center().x() - card.width() / 2), focus.top() - GAP - card.height());
        if (focus.right() + GAP + card.width() <= area.right())
            return QPoint(focus.right() + GAP, clampY(focus.center().y() - card.height() / 2));
        if (focus.left() - GAP - card.width() >= area.left())
            return QPoint(focus.left() - GAP - card.width(), clampY(focus.center().y() - card.height() / 2));

        // A large part (the canvas): over it, in its lower right corner.
        return QPoint(clampX(focus.right() - 28 - card.width()), clampY(focus.bottom() - 28 - card.height()));
    }

    void TutorialOverlay::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        std::vector<QRectF> holes;
        for (const QRectF& r : shownHoles())
            if (r.width() > 1 && r.height() > 1) holes.push_back(r);

        QPainterPath dim;
        dim.addRect(QRectF(rect()));
        QPainterPath cut;
        for (const QRectF& h : holes) cut.addRoundedRect(h, 12, 12);
        p.fillPath(dim.subtracted(cut), QColor(15, 17, 28, 150));

        // Just outside the hole: inside it, the window is drawn instead.
        for (const QRectF& h : holes) {
            const QRectF ring = h.adjusted(-4, -4, 4, 4);
            QColor glow = style::palette::accent;
            glow.setAlpha(70);
            p.setPen(QPen(glow, 7));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(ring, 15, 15);
            p.setPen(QPen(style::palette::accent, 2));
            p.drawRoundedRect(ring.adjusted(-2, -2, 2, 2), 17, 17);
        }
    }

    std::vector<QRectF> TutorialOverlay::shownHoles() const {
        if (morph_ >= 1.0) return to_;
        // Paired one to one; a missing partner is the other side's outline as
        // a whole, or, when that side has none, a point (growing or shrinking).
        std::vector<QRectF> shown;
        const size_t m = std::max(from_.size(), to_.size());
        for (size_t i = 0; i < m; ++i) {
            QRectF a = i < from_.size() ? from_[i] : unite(from_);
            QRectF b = i < to_.size() ? to_[i] : unite(to_);
            if (a.isNull()) a = QRectF(b.center(), QSizeF(0, 0));
            if (b.isNull()) b = QRectF(a.center(), QSizeF(0, 0));
            shown.push_back(lerp(a, b, morph_));
        }
        return shown;
    }

    bool TutorialOverlay::eventFilter(QObject* watched, QEvent* event) {
        if (watched == parentWidget() && event->type() == QEvent::Resize) {
            setGeometry(parentWidget()->rect());
            refresh(false);
        }
        if (watched == card_ && (event->type() == QEvent::Move || event->type() == QEvent::Resize))
            updateMask();
        return QWidget::eventFilter(watched, event);
    }

    void TutorialOverlay::keyPressEvent(QKeyEvent* event) {
        switch (event->key()) {
        case Qt::Key_Right:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            next_->click();
            break;
        case Qt::Key_Left:
            go(current_ - 1);
            break;
        case Qt::Key_Escape:
            finish(false);
            break;
        default:
            break;
        }
        event->accept();
    }

    // The window underneath does not react while the tour is on.
    void TutorialOverlay::mousePressEvent(QMouseEvent* event) { event->accept(); }
    void TutorialOverlay::wheelEvent(QWheelEvent* event) { event->accept(); }

    void TutorialOverlay::finish(bool completed) {
        follow_timer_.stop();
        if (parentWidget()) parentWidget()->removeEventFilter(this);
        emit finished(completed);
        close(); // deletes it (WA_DeleteOnClose)
    }

} // namespace ui::tutorial
