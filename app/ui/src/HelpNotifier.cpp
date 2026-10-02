#include "HelpNotifier.h"
#include "UiStyle.h"

#include <QAction>
#include <QEvent>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QSettings>
#include <QVariantAnimation>

namespace ui {

    namespace {
        const QString FLAGS_KEY = QStringLiteral("help/flaggedEntries");
    }

    // ============================================================================
    // NotificationDot
    // ============================================================================

    NotificationDot::NotificationDot(QWidget* parent) : QWidget(parent) {
        setFixedSize(SIZE, SIZE);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        pulse_ = new QVariantAnimation(this);
        pulse_->setStartValue(0.0);
        pulse_->setEndValue(1.0);
        pulse_->setDuration(1600);
        pulse_->setLoopCount(-1);
        connect(pulse_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            phase_ = v.toDouble();
            update();
        });
        hide();
    }

    void NotificationDot::centreAt(QPoint point) {
        move(point - QPoint(SIZE / 2, SIZE / 2));
        raise();
    }

    void NotificationDot::showEvent(QShowEvent*) { pulse_->start(); }
    void NotificationDot::hideEvent(QHideEvent*) { pulse_->stop(); }

    void NotificationDot::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(SIZE / 2.0, SIZE / 2.0);

        // A ring that grows and fades, then the dot itself.
        QColor halo = style::palette::accent;
        halo.setAlphaF(0.45 * (1.0 - phase_));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(c, 4.0 + 5.5 * phase_, 4.0 + 5.5 * phase_);

        QLinearGradient g(c - QPointF(4, 4), c + QPointF(4, 4));
        g.setColorAt(0, style::palette::accent);
        g.setColorAt(1, style::palette::violet);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(g);
        p.drawEllipse(c, 4.5, 4.5);
    }

    // ============================================================================
    // HelpNotifier
    // ============================================================================

    HelpNotifier::HelpNotifier(QMenuBar* bar, QMenu* help_menu, QObject* parent)
        : QObject(parent), bar_(bar), menu_(help_menu), bar_dot_(new NotificationDot(bar))
    {
        QSettings settings;
        flagged_ = storedFlags(settings);

        bar_->installEventFilter(this);
        menu_->installEventFilter(this);
        connect(menu_, &QMenu::aboutToShow, this, [this] { bar_dot_->hide(); });
        connect(menu_, &QMenu::aboutToHide, this, [this] { clear(); });
    }

    QStringList HelpNotifier::storedFlags(QSettings& settings) {
        return settings.value(FLAGS_KEY).toStringList();
    }

    void HelpNotifier::storeFlags(QSettings& settings, const QStringList& keys) {
        if (keys.isEmpty()) settings.remove(FLAGS_KEY);
        else                settings.setValue(FLAGS_KEY, keys);
    }

    void HelpNotifier::addEntry(const QString& key, QAction* action) {
        entries_[key] = action;
        placeBarDot();
    }

    void HelpNotifier::flag(const QString& key) {
        if (!flagged_.contains(key)) flagged_.append(key);
        save();
        placeBarDot();
    }

    void HelpNotifier::save() const {
        QSettings settings;
        storeFlags(settings, flagged_);
    }

    void HelpNotifier::clear() {
        for (auto& [key, dot] : entry_dots_) dot->hide();
        if (flagged_.isEmpty()) return;
        flagged_.clear();
        save();
    }

    void HelpNotifier::placeBarDot() {
        // Only for entries this menu has (a stored flag may name one that no
        // longer exists).
        bool any = false;
        for (const QString& key : flagged_) any = any || entries_.count(key);
        if (!any || menu_->isVisible()) {
            bar_dot_->hide();
            return;
        }
        const QRect title = bar_->actionGeometry(menu_->menuAction());
        if (title.isEmpty()) return;
        bar_dot_->centreAt(QPoint(title.right() - 3, title.top() + 7));
        bar_dot_->show();
    }

    void HelpNotifier::placeEntryDots() {
        for (const QString& key : flagged_) {
            auto it = entries_.find(key);
            if (it == entries_.end()) continue;
            const QRect r = menu_->actionGeometry(it->second);
            if (r.isEmpty()) continue;
            NotificationDot*& dot = entry_dots_[key];
            if (!dot) dot = new NotificationDot(menu_);
            dot->centreAt(QPoint(r.right() - 16, r.center().y()));
            dot->show();
        }
    }

    bool HelpNotifier::eventFilter(QObject* watched, QEvent* event) {
        if (watched == menu_ && event->type() == QEvent::Show) placeEntryDots();
        if (watched == bar_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show
            || event->type() == QEvent::LayoutRequest || event->type() == QEvent::Polish))
            placeBarDot();
        return QObject::eventFilter(watched, event);
    }

} // namespace ui
