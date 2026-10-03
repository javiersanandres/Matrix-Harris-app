#include "GurobiGuide.h"
#include "AppDialogs.h"
#include "PictureViewer.h"
#include "UiStyle.h"

#include <QDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QTextDocument>
#include <QTimer>
#include <QVariantAnimation>
#include <QElapsedTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace ui::gurobi {

    namespace {

        // ============================================================================
        // Content
        // ============================================================================

        // One screenshot: its number in the guide's folder (01.png, 02.png, ...)
        // and, when a step has several, what this one shows.
        struct Shot {
            int image;
            QString caption;
        };

        struct Step {
            QString text; // rich text
            std::vector<Shot> shots;
        };

        struct GuideContent {
            QString title;
            QString folder; // under :/gurobi/
            std::vector<Step> steps;
        };

        const QString REQUIREMENT = QStringLiteral(
            "Para completar estos pasos necesitas ser estudiante, profesor o investigador "
            "universitario y una cuenta de correo educativa asociada.");

        const QString GUROBI_LINK = QStringLiteral(
            "<a href=\"https://www.gurobi.com/\" style=\"color:#4F46E5;\">www.gurobi.com</a>");

        GuideContent installGuide() {
            return { QStringLiteral("Cómo instalar Gurobi"), QStringLiteral("instalar"), {
                { QStringLiteral("Entra en %1, abre <b>Sign In</b> y, en el menú desplegable, haz clic "
                                 "en <b>Register</b>.").arg(GUROBI_LINK),
                  { { 1, {} } } },
                { QStringLiteral("Rellena todos los campos con tu <b>correo institucional</b> y haz clic en <b>Next</b>."),
                  { { 2, {} } } },
                { QStringLiteral("Selecciona <b>Academic</b>, introduce los datos de tu universidad y haz clic "
                                 "en <b>Next</b>."),
                  { { 3, {} } } },
                { QStringLiteral("Escribe una contraseña y pulsa <b>Submit</b>."),
                  { { 4, {} } } },
                { QStringLiteral("Busca en tu correo el código de verificación, escríbelo y pulsa "
                                 "<b>Confirm account and Sign In</b>."),
                  { { 5, {} } } },
                { QStringLiteral("Llegarás al portal de usuario. Abre <b>Downloads</b> y elige "
                                 "<b>Download Center</b>."),
                  { { 6, {} } } },
                { QStringLiteral("En la página de descargas, baja hasta <b>Gurobi Optimizer</b> y, en la fila "
                                 "<b>x64 Windows</b>, haz clic en <b>Installer</b> para descargarlo."),
                  { { 7, {} } } },
                { QStringLiteral("Abre el archivo descargado. En el instalador pulsa <b>Next</b>, acepta los "
                                 "términos de uso y sigue con <b>Next</b> hasta llegar a <b>Install</b>."),
                  { { 8, QStringLiteral("Pantalla de bienvenida: pulsa «Next».") },
                    { 9, QStringLiteral("Marca la casilla para aceptar los términos y pulsa «Next».") },
                    { 10, QStringLiteral("Deja la carpeta propuesta y pulsa «Next».") },
                    { 11, QStringLiteral("Pulsa «Install».") } } },
                { QStringLiteral("Espera a que termine la instalación y pulsa <b>Finish</b>. Si se abre algún "
                                 "archivo automáticamente, no te preocupes: simplemente ciérralo."),
                  { { 12, {} } } },
                { QStringLiteral("Te pedirá reiniciar el ordenador para que los cambios surtan efecto: pulsa "
                                 "<b>Yes</b>. Después solo te faltará la licencia gratuita: la guía está en "
                                 "<b>Ayuda › Cómo obtener una licencia de Gurobi</b>."),
                  { { 13, {} } } },
            } };
        }

        GuideContent licenseGuide() {
            return { QStringLiteral("Cómo obtener una licencia de Gurobi"), QStringLiteral("licencia"), {
                { QStringLiteral("<b>Conéctate a la red de tu universidad</b>, ya sea Eduroam o por VPN, y entra "
                                 "en %1. Abre <b>Sign In</b> y elige <b>Customer Login</b>, o haz clic en "
                                 "<b>My Account</b>.").arg(GUROBI_LINK),
                  { { 1, QStringLiteral("Sign In › Customer Login.") },
                    { 2, QStringLiteral("O bien, directamente, «My Account».") } } },
                { QStringLiteral("Escribe el correo con el que te registraste y pulsa <b>Next</b>. A continuación, "
                                 "introduce tu contraseña y pulsa <b>Sign in</b>."),
                  { { 3, QStringLiteral("Tu correo, y «Next».") },
                    { 4, QStringLiteral("Tu contraseña, y «Sign in».") } } },
                { QStringLiteral("En el portal de usuario, pulsa <b>Request a license</b>."),
                  { { 5, {} } } },
                { QStringLiteral("De las opciones que aparecen, elige la del medio, <b>Named-User Academic</b>, con "
                                 "<b>Generate now!</b>, y confirma con <b>Confirm Request</b>. Si da un error, lo "
                                 "más probable es que no estés en la red universitaria: conéctate e inténtalo de nuevo."),
                  { { 6, QStringLiteral("«Generate now!» en la opción del medio.") },
                    { 7, QStringLiteral("Acepta las condiciones y pulsa «Confirm Request».") } } },
                { QStringLiteral("Aparecerá un mensaje con un comando que empieza por <b>grbgetkey</b>: cópialo. "
                                 "Si lo cierras, puedes volver a verlo cuando quieras en <b>Licenses</b>, con el "
                                 "icono de la pantalla de ordenador."),
                  { { 8, QStringLiteral("Copia el comando completo.") },
                    { 9, QStringLiteral("En «Licenses», el icono de la pantalla muestra de nuevo las instrucciones.") } } },
                { QStringLiteral("Abre el menú Inicio, escribe <b>Símbolo del sistema</b> y haz clic en "
                                 "<b>Ejecutar como administrador</b>."),
                  { { 10, {} } } },
                { QStringLiteral("Se abrirá una terminal. No te asustes: es simplemente otra forma de interactuar con "
                                 "el ordenador. Pega el comando del paso 5 con <b>Ctrl + V</b> o haciendo "
                                 "<b>clic derecho</b>."),
                  { { 11, {} } } },
                { QStringLiteral("Pulsa <b>Enter</b>. Te preguntará dónde guardar la licencia: vuelve a pulsar "
                                 "<b>Enter</b> para dejarla en la carpeta propuesta."),
                  { { 12, {} } } },
                { QStringLiteral("Verás dónde se ha guardado la licencia. Cierra la terminal y… ¡enhorabuena! "
                                 "Vuelve a abrir el Taller Matrix Harris para disfrutar de su mejor versión."),
                  { { 13, {} } } },
            } };
        }

        // ============================================================================
        // Pieces
        // ============================================================================

        // The guide's emblem, in the place of the dialog's badge: the usual
        // indigo disc with a download arrow (installing) or a key (license).
        class GuideBadge : public QWidget {
        public:
            GuideBadge(Guide guide, QWidget* parent) : QWidget(parent), guide_(guide) {
                setFixedSize(48, 48);
            }

        protected:
            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                const QPointF c(24, 24);
                p.setPen(Qt::NoPen);
                p.setBrush(style::palette::accent_soft);
                p.drawEllipse(c, 24, 24);
                QLinearGradient g(c - QPointF(15, 15), c + QPointF(15, 15));
                g.setColorAt(0, style::palette::accent.lighter(112));
                g.setColorAt(1, style::palette::violet);
                p.setBrush(g);
                p.drawEllipse(c, 15, 15);

                p.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);
                if (guide_ == Guide::Install) {
                    // An arrow dropping into a tray.
                    p.drawLine(c + QPointF(0, -7.5), c + QPointF(0, 2.5));
                    p.drawPolyline(QPolygonF({ c + QPointF(-4, -1.5), c + QPointF(0, 2.5), c + QPointF(4, -1.5) }));
                    p.drawPolyline(QPolygonF({ c + QPointF(-7, 3), c + QPointF(-7, 7), c + QPointF(7, 7), c + QPointF(7, 3) }));
                }
                else {
                    // A key: round bow, shaft and two teeth.
                    p.drawEllipse(c + QPointF(-4, -3.5), 3.6, 3.6);
                    p.drawLine(c + QPointF(-1.5, -1), c + QPointF(6.5, 7));
                    p.drawLine(c + QPointF(2.5, 3), c + QPointF(4.5, 1));
                    p.drawLine(c + QPointF(4.5, 5), c + QPointF(6.5, 3));
                }
            }

        private:
            Guide guide_;
        };

        // A rounded amber warning triangle, for the requirement banner.
        QPixmap warningPixmap(int size, qreal dpr) {
            QPixmap pm(QSize(size, size) * dpr);
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const double s = size;
            QPainterPath tri;
            tri.moveTo(s / 2, s * 0.08);
            tri.lineTo(s * 0.96, s * 0.88);
            tri.lineTo(s * 0.04, s * 0.88);
            tri.closeSubpath();
            QLinearGradient g(0, 0, 0, s);
            g.setColorAt(0, QColor(0xFB, 0xBF, 0x24));
            g.setColorAt(1, style::palette::amber);
            p.setPen(QPen(style::palette::amber, s * 0.09, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(g);
            p.drawPath(tri);
            p.setPen(QPen(Qt::white, s * 0.11, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(s / 2, s * 0.38), QPointF(s / 2, s * 0.6));
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(s / 2, s * 0.74), s * 0.06, s * 0.06);
            return pm;
        }

        QWidget* makeRequirementBanner(QWidget* parent) {
            auto* banner = new QFrame(parent);
            banner->setObjectName("requirement");
            banner->setStyleSheet(QStringLiteral(
                "QFrame#requirement { background: #FFF8E6; border: 1px solid #F6D58A; border-radius: 10px; }"
                "QLabel { background: transparent; border: none; }"));
            auto* row = new QHBoxLayout(banner);
            row->setContentsMargins(12, 10, 14, 10);
            row->setSpacing(12);
            auto* icon = new QLabel(banner);
            icon->setPixmap(warningPixmap(24, parent ? parent->devicePixelRatioF() : qApp->devicePixelRatio()));
            row->addWidget(icon, 0, Qt::AlignVCenter);
            auto* text = new QLabel(QStringLiteral("<b>%1</b>").arg(REQUIREMENT), banner);
            text->setWordWrap(true);
            text->setStyleSheet(QStringLiteral("color: #7A4E00; font-size: 10pt;"));
            row->addWidget(text, 1);
            return banner;
        }

        // The step's number in a small gradient disc.
        class StepNumber : public QWidget {
        public:
            explicit StepNumber(QWidget* parent) : QWidget(parent) { setFixedSize(30, 30); }
            void setNumber(int n) { number_ = n; update(); }

        protected:
            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                QLinearGradient g(0, 0, 30, 30);
                g.setColorAt(0, style::palette::accent);
                g.setColorAt(1, style::palette::violet);
                p.setPen(Qt::NoPen);
                p.setBrush(g);
                p.drawEllipse(QRectF(1, 1, 28, 28));
                QFont f = font();
                f.setPixelSize(14);
                f.setBold(true);
                p.setFont(f);
                p.setPen(Qt::white);
                p.drawText(QRectF(0, 0, 30, 30), Qt::AlignCenter, QString::number(number_));
            }

        private:
            int number_ = 1;
        };

        // The screenshot, fitted into a fixed frame (so the dialog never
        // changes size between slides), faded in on each change. A click shows
        // it full screen; a missing one shows a placeholder naming the file.
        class ShotView : public QWidget {
        public:
            ShotView(QSize size, QWidget* parent) : QWidget(parent) {
                setFixedSize(size);
                QObject::connect(&frame_timer_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
            }

            void setImage(const QString& resource) {
                resource_ = resource;
                source_ = QPixmap(resource);
                fitted_ = QPixmap();
                setCursor(source_.isNull() ? Qt::ArrowCursor : Qt::PointingHandCursor);
                setToolTip(source_.isNull() ? QString() : QStringLiteral("Haz clic para ampliar"));
                // Faded in over the next frames (see paintEvent).
                shown_at_.start();
                frame_timer_.start(16);
                update();
            }

            const QPixmap& source() const { return source_; }
            std::function<void()> on_click;

        protected:
            void enterEvent(QEnterEvent*) override { hovered_ = true; update(); }
            void leaveEvent(QEvent*) override { hovered_ = false; update(); }

            void mouseReleaseEvent(QMouseEvent* event) override {
                if (event->button() == Qt::LeftButton && !source_.isNull() && rect().contains(event->pos()) && on_click)
                    on_click();
            }
            // Taken here, so a click on the picture does not start dragging the dialog.
            void mousePressEvent(QMouseEvent* event) override { event->accept(); }

            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
                QPainterPath rounded;
                rounded.addRoundedRect(frame, 12, 12);
                p.fillPath(rounded, style::palette::surface);

                if (source_.isNull()) {
                    QPen dashed(style::palette::faint, 1.4, Qt::DashLine);
                    p.setPen(dashed);
                    p.setBrush(Qt::NoBrush);
                    p.drawRoundedRect(frame.adjusted(10, 10, -10, -10), 9, 9);
                    QFont f = font();
                    f.setPointSizeF(10.5);
                    f.setBold(true);
                    p.setFont(f);
                    p.setPen(style::palette::muted);
                    const QRectF r = frame.adjusted(20, 20, -20, -20);
                    p.drawText(r.adjusted(0, 0, 0, -r.height() / 2 + 4), Qt::AlignHCenter | Qt::AlignBottom,
                        QStringLiteral("Captura pendiente"));
                    f.setBold(false);
                    f.setPointSizeF(9);
                    p.setFont(f);
                    p.drawText(r.adjusted(0, r.height() / 2 + 6, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
                        QString(resource_).replace(QStringLiteral(":/"), QStringLiteral("app/ui/resources/")));
                    p.setPen(QPen(style::palette::line, 1.0));
                    p.drawPath(rounded);
                    return;
                }

                // The picture, as large as fits, scaled once per size with a
                // smooth filter (screenshots are much bigger than the frame).
                const QRectF area = frame.adjusted(10, 10, -10, -10);
                QSizeF fit = QSizeF(source_.size()).scaled(area.size(), Qt::KeepAspectRatio);
                const QRectF target(area.center() - QPointF(fit.width() / 2, fit.height() / 2), fit);
                const qreal dpr = devicePixelRatioF();
                const QSize wanted = (target.size() * dpr).toSize();
                if (fitted_.size() != wanted) {
                    fitted_ = source_.scaled(wanted, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    fitted_.setDevicePixelRatio(dpr);
                }
                // Fade in: an ease-out over FADE_MS from the change.
                const double t = shown_at_.isValid() ? std::min(1.0, shown_at_.elapsed() / FADE_MS) : 1.0;
                if (t >= 1.0) frame_timer_.stop();
                p.setOpacity(1.0 - std::pow(1.0 - t, 3.0));
                QPainterPath clip;
                clip.addRoundedRect(target, 6, 6);
                p.save();
                p.setClipPath(clip);
                p.drawPixmap(target.topLeft(), fitted_);
                p.restore();
                p.setPen(QPen(style::palette::line, 1.0));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(target, 6, 6);
                p.setOpacity(1.0);

                // "Ampliar" pill while hovered.
                if (hovered_) {
                    QFont f = font();
                    f.setPointSizeF(9);
                    f.setBold(true);
                    p.setFont(f);
                    const QString label = QStringLiteral("Ampliar");
                    const double w = QFontMetricsF(f).horizontalAdvance(label) + 40;
                    const QRectF pill(target.right() - w - 10, target.bottom() - 38, w, 28);
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(31, 35, 48, 190));
                    p.drawRoundedRect(pill, 14, 14);
                    // A small magnifier.
                    const QPointF mc = pill.topLeft() + QPointF(16, 13);
                    p.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(mc, 4.5, 4.5);
                    p.drawLine(mc + QPointF(3.3, 3.3), mc + QPointF(6.5, 6.5));
                    p.setPen(Qt::white);
                    p.drawText(pill.adjusted(28, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
                }
                p.setPen(QPen(style::palette::line, 1.0));
                p.setBrush(Qt::NoBrush);
                p.drawPath(rounded);
            }

        private:
            QString resource_;
            QPixmap source_;
            QPixmap fitted_;
            static constexpr double FADE_MS = 220.0;
            QElapsedTimer shown_at_;
            QTimer frame_timer_;
            bool hovered_ = false;
        };

        // One dot per step: the current one stretched into a gradient pill, the
        // ones already seen tinted. A click on a dot jumps to that step.
        class StepDots : public QWidget {
        public:
            StepDots(int count, QWidget* parent) : QWidget(parent), count_(count) {
                setFixedSize(count_ * (DOT + GAP) - GAP + (PILL - DOT), 18);
                setMouseTracking(true);
                setCursor(Qt::PointingHandCursor);
            }

            void setCurrent(int index) { current_ = index; update(); }
            std::function<void(int)> on_pick;

        protected:
            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(Qt::NoPen);
                for (int i = 0; i < count_; ++i) {
                    const QRectF r = dotRect(i);
                    if (i == current_) {
                        QLinearGradient g(r.topLeft(), r.topRight());
                        g.setColorAt(0, style::palette::accent);
                        g.setColorAt(1, style::palette::violet);
                        p.setBrush(g);
                    }
                    else {
                        QColor c = i < current_ ? QColor(0xC7, 0xC9, 0xF9) : QColor(0xD6, 0xDA, 0xE4);
                        if (i == hovered_) c = c.darker(118);
                        p.setBrush(c);
                    }
                    p.drawRoundedRect(r, DOT / 2.0, DOT / 2.0);
                }
            }

            void mouseMoveEvent(QMouseEvent* event) override {
                const int i = indexAt(event->position().x());
                if (i != hovered_) {
                    hovered_ = i;
                    setToolTip(i >= 0 ? QStringLiteral("Paso %1").arg(i + 1) : QString());
                    update();
                }
            }
            void leaveEvent(QEvent*) override { hovered_ = -1; update(); }
            void mousePressEvent(QMouseEvent* event) override {
                event->accept();
                const int i = indexAt(event->position().x());
                if (i >= 0 && on_pick) on_pick(i);
            }

        private:
            static constexpr int DOT = 8, PILL = 24, GAP = 7;

            QRectF dotRect(int i) const {
                double x = i * (DOT + GAP) + (i > current_ ? PILL - DOT : 0);
                return QRectF(x, (height() - DOT) / 2.0, i == current_ ? PILL : DOT, DOT);
            }
            int indexAt(double x) const {
                for (int i = 0; i < count_; ++i)
                    if (dotRect(i).adjusted(-GAP / 2.0, 0, GAP / 2.0, 0).contains(x, height() / 2.0)) return i;
                return -1;
            }

            int count_;
            int current_ = 0;
            int hovered_ = -1;
        };

        // Height that the tallest of `texts` takes as rich text at `width`, so
        // the text area can keep one size for every step.
        int tallestText(const std::vector<QString>& texts, int width, const QFont& font) {
            double tallest = 0.0;
            for (const QString& t : texts) {
                QTextDocument doc;
                doc.setDefaultFont(font);
                doc.setDocumentMargin(0);
                doc.setHtml(t);
                doc.setTextWidth(width);
                tallest = std::max(tallest, doc.size().height());
            }
            return static_cast<int>(std::ceil(tallest)) + 2;
        }

    } // namespace

    void showGuide(Guide guide, QWidget* parent) {
        const GuideContent content = guide == Guide::Install ? installGuide() : licenseGuide();

        struct Slide { int step; int shot; };
        std::vector<Slide> slides;
        for (int s = 0; s < static_cast<int>(content.steps.size()); ++s)
            for (int k = 0; k < static_cast<int>(content.steps[s].shots.size()); ++k)
                slides.push_back({ s, k });

        StyledDialog dlg(StyledDialog::Badge::Info, content.title, parent);
        dlg.setEmblem(new GuideBadge(guide, nullptr));

        // The screenshot frame takes most of the screen it opens on, leaving
        // room for the rest of the dialog (about 400 px of it).
        QScreen* screen = parent && parent->window()->screen() ? parent->window()->screen()
                                                              : QGuiApplication::primaryScreen();
        const QRect avail = screen->availableGeometry();
        constexpr double ASPECT = 1.9; // the screenshots are about this wide for their height
        int shot_w = std::clamp(static_cast<int>(avail.width() * 0.55), 520, 860);
        int shot_h = static_cast<int>(shot_w / ASPECT);
        const int max_h = std::max(200, avail.height() - 430);
        if (shot_h > max_h) {
            shot_h = max_h;
            shot_w = std::max(520, static_cast<int>(shot_h * ASPECT));
        }

        dlg.setMessage(QString()); // created now, so it sits under the title (filled per slide)

        auto* body = new QWidget;
        body->setFixedWidth(shot_w);
        auto* col = new QVBoxLayout(body);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(12);
        col->addWidget(makeRequirementBanner(body));
        col->addSpacing(4);

        auto* step_row = new QHBoxLayout;
        step_row->setSpacing(12);
        auto* number = new StepNumber(body);
        step_row->addWidget(number, 0, Qt::AlignTop);
        auto* text = new QLabel(body);
        text->setWordWrap(true);
        text->setTextFormat(Qt::RichText);
        text->setOpenExternalLinks(true);
        text->setTextInteractionFlags(Qt::TextBrowserInteraction);
        text->setStyleSheet(QStringLiteral("color: #1F2330; font-size: 10.5pt;"));
        text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        {
            // Every step's text in the same room, so nothing below it jumps.
            QFont f = text->font();
            f.setPointSizeF(10.5);
            std::vector<QString> texts;
            for (const Step& s : content.steps) texts.push_back(s.text);
            text->setFixedHeight(std::max(30, tallestText(texts, shot_w - 42, f)));
        }
        step_row->addWidget(text, 1);
        col->addLayout(step_row);

        auto* shot = new ShotView(QSize(shot_w, shot_h), body);
        col->addWidget(shot);

        auto* caption = new QLabel(body);
        caption->setAlignment(Qt::AlignCenter);
        caption->setStyleSheet(QStringLiteral("color: #4B5068; font-size: 9.5pt;"));
        caption->setFixedHeight(caption->fontMetrics().height() + 4);
        col->addWidget(caption);

        auto* dots = new StepDots(static_cast<int>(content.steps.size()), body);
        col->addWidget(dots, 0, Qt::AlignHCenter);
        dlg.setBody(body);

        dlg.addButton(QStringLiteral("Cerrar"), 0, StyledDialog::ButtonStyle::Secondary, false, true);
        QPushButton* prev = dlg.addActionButton(QStringLiteral("‹  Anterior"), StyledDialog::ButtonStyle::Secondary);
        // Greyed out on the first slide (on the button itself: the dialog's
        // sheet would not win over its own rule for secondary buttons).
        prev->setStyleSheet(QStringLiteral(
            "QPushButton:disabled { color: #B8BDCB; border-color: #E8EAF0; background: white; }"));
        QPushButton* next = dlg.addActionButton(QStringLiteral("Siguiente  ›"), StyledDialog::ButtonStyle::Primary);
        next->setDefault(true);

        int current = -1;
        auto go = [&](int index) {
            index = std::clamp(index, 0, static_cast<int>(slides.size()) - 1);
            if (index == current) return;
            current = index;
            const Slide& slide = slides[index];
            const Step& step = content.steps[slide.step];
            const int shots = static_cast<int>(step.shots.size());
            const Shot& s = step.shots[slide.shot];

            number->setNumber(slide.step + 1);
            text->setText(step.text);
            shot->setImage(QStringLiteral(":/gurobi/%1/%2.png").arg(content.folder)
                .arg(s.image, 2, 10, QLatin1Char('0')));
            caption->setText(shots > 1
                ? QStringLiteral("<b>%1 / %2</b>&nbsp;&nbsp;%3").arg(slide.shot + 1).arg(shots).arg(s.caption.toHtmlEscaped())
                : QString());
            dots->setCurrent(slide.step);
            dlg.setMessage(QStringLiteral("Paso %1 de %2").arg(slide.step + 1).arg(content.steps.size()));

            prev->setEnabled(index > 0);
            next->setText(index + 1 < static_cast<int>(slides.size())
                ? QStringLiteral("Siguiente  ›") : QStringLiteral("Terminar"));
        };
        auto forward = [&] {
            if (current + 1 < static_cast<int>(slides.size())) go(current + 1);
            else dlg.accept();
        };

        QObject::connect(prev, &QPushButton::clicked, &dlg, [&] { go(current - 1); });
        QObject::connect(next, &QPushButton::clicked, &dlg, forward);
        dots->on_pick = [&](int step) {
            for (int i = 0; i < static_cast<int>(slides.size()); ++i)
                if (slides[i].step == step) { go(i); return; }
        };
        shot->on_click = [&] {
            showPictureFullScreen(shot->source(), &dlg);
        };
        auto* right = new QShortcut(QKeySequence(Qt::Key_Right), &dlg);
        QObject::connect(right, &QShortcut::activated, &dlg, [&] { go(current + 1); });
        auto* left = new QShortcut(QKeySequence(Qt::Key_Left), &dlg);
        QObject::connect(left, &QShortcut::activated, &dlg, [&] { go(current - 1); });

        go(0);
        dlg.exec();
    }

} // namespace ui::gurobi
