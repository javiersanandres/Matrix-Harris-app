#include "PictureViewer.h"

#include <QDialog>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QVariantAnimation>

namespace ui {

    namespace {

        // A screenshot full screen over a dimmed background. Any click or Esc
        // closes it.
        class Lightbox : public QDialog {
        public:
            Lightbox(const QPixmap& picture, QWidget* parent)
                : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint), picture_(picture)
            {
                setAttribute(Qt::WA_TranslucentBackground);
                setModal(true);
                QScreen* screen = parent && parent->window()->screen() ? parent->window()->screen()
                                                                      : QGuiApplication::primaryScreen();
                setGeometry(screen->geometry());
                setCursor(Qt::PointingHandCursor);
                setWindowOpacity(0.0);
                auto* fade = new QVariantAnimation(this);
                fade->setDuration(160);
                fade->setStartValue(0.0);
                fade->setEndValue(1.0);
                connect(fade, &QVariantAnimation::valueChanged, this,
                    [this](const QVariant& v) { setWindowOpacity(v.toDouble()); });
                fade->start(QAbstractAnimation::DeleteWhenStopped);
            }

        protected:
            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                p.fillRect(rect(), QColor(15, 17, 26, 215));
                const QRectF area = QRectF(rect()).adjusted(48, 40, -48, -76);
                const QSizeF fit = QSizeF(picture_.size()).scaled(area.size(), Qt::KeepAspectRatio)
                    .boundedTo(QSizeF(picture_.size()) / picture_.devicePixelRatio() * 1.6);
                const QRectF target(area.center() - QPointF(fit.width() / 2, fit.height() / 2), fit);
                QPainterPath clip;
                clip.addRoundedRect(target, 10, 10);
                p.setClipPath(clip);
                p.drawPixmap(target, picture_, QRectF(picture_.rect()));
                p.setClipping(false);

                QFont f = font();
                f.setPointSizeF(10);
                p.setFont(f);
                p.setPen(QColor(255, 255, 255, 170));
                p.drawText(QRectF(0, rect().bottom() - 60, width(), 40), Qt::AlignCenter,
                    QStringLiteral("Haz clic en cualquier sitio o pulsa Esc para volver"));
            }
            void mousePressEvent(QMouseEvent* event) override {
                event->accept();
                accept();
            }

        private:
            QPixmap picture_;
        };

    } // namespace

    void showPictureFullScreen(const QPixmap& picture, QWidget* parent) {
        Lightbox box(picture, parent);
        box.exec();
    }

} // namespace ui
