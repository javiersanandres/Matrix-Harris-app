#include "DiagramExport.h"
#include "UiStyle.h"

#include <QApplication>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QImage>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>

#include <algorithm>

namespace ui::exporting {

    namespace {

        // Everything on a PDF page is laid out in points (1/72 inch).
        constexpr double MARGIN = 40.0;
        constexpr double HEADER = 62.0;  // title, project name and accent rule
        constexpr double FOOTER = 26.0;

        // Room kept around a diagram's items so nothing touches the border.
        QRectF sourceRect(QGraphicsScene* scene) {
            return scene->itemsBoundingRect().adjusted(-24, -24, 24, 24);
        }

        QPageLayout layoutFor(QGraphicsScene* scene) {
            // Landscape for drawings wider than tall, portrait otherwise.
            const bool wide = scene && !scene->items().isEmpty()
                && sourceRect(scene).width() > sourceRect(scene).height();
            return QPageLayout(QPageSize(QPageSize::A4),
                wide ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(0, 0, 0, 0));
        }

        QFont font(double points, bool bold = false, bool italic = false) {
            QFont f = QApplication::font();
            f.setPointSizeF(points);
            f.setBold(bold);
            f.setItalic(italic);
            return f;
        }

        void paintPage(QPainter& p, const QSizeF& page, const Page& content,
            const QString& project_name, int number, int total)
        {
            const double width = page.width() - 2 * MARGIN;

            // Header: diagram name, project name, short accent rule.
            p.setPen(style::palette::ink);
            p.setFont(font(18, true));
            p.drawText(QRectF(MARGIN, MARGIN, width, 26), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetricsF(p.font()).elidedText(content.title, Qt::ElideRight, width));
            p.setPen(style::palette::muted);
            p.setFont(font(9));
            p.drawText(QRectF(MARGIN, MARGIN + 28, width, 14), Qt::AlignLeft | Qt::AlignVCenter, project_name);
            QLinearGradient rule(MARGIN, 0, MARGIN + 64, 0);
            rule.setColorAt(0, style::palette::accent);
            rule.setColorAt(1, style::palette::violet);
            p.setPen(Qt::NoPen);
            p.setBrush(rule);
            p.drawRoundedRect(QRectF(MARGIN, MARGIN + 48, 64, 3), 1.5, 1.5);

            // Footer: application and page number, over a hairline.
            const double footer_top = page.height() - MARGIN - FOOTER + 8;
            p.setPen(QPen(style::palette::line, 0.6));
            p.drawLine(QPointF(MARGIN, footer_top), QPointF(page.width() - MARGIN, footer_top));
            p.setPen(style::palette::muted);
            p.setFont(font(8));
            const QRectF footer(MARGIN, footer_top + 4, width, 14);
            p.drawText(footer, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Taller Matrix Harris"));
            p.drawText(footer, Qt::AlignRight | Qt::AlignVCenter,
                QStringLiteral("Página %1 de %2").arg(number).arg(total));

            // The drawing, scaled down to fit (never up) and centred.
            const QRectF area(MARGIN, MARGIN + HEADER + 14, width,
                footer_top - (MARGIN + HEADER + 14) - 14);
            if (!content.scene || content.scene->items().isEmpty()) {
                p.setPen(style::palette::faint);
                p.setFont(font(11, false, true));
                p.drawText(area, Qt::AlignCenter, QStringLiteral("Este esquema está vacío"));
                return;
            }
            const QRectF source = sourceRect(content.scene);
            const double scale = std::min({ area.width() / source.width(), area.height() / source.height(), 1.0 });
            QRectF target(0, 0, source.width() * scale, source.height() * scale);
            target.moveCenter(area.center());
            content.scene->render(&p, target, source);
        }

    } // namespace

    bool writePdf(const QString& path, const QString& project_name, const std::vector<Page>& pages) {
        if (pages.empty()) return false;

        QPdfWriter pdf(path);
        pdf.setResolution(72); // painter units are points
        pdf.setTitle(project_name);
        pdf.setCreator(QStringLiteral("Taller Matrix Harris"));
        pdf.setPageLayout(layoutFor(pages.front().scene));

        QPainter p;
        if (!p.begin(&pdf)) return false;
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);

        for (size_t i = 0; i < pages.size(); ++i) {
            if (i > 0) {
                // Each page takes the orientation its own drawing needs.
                pdf.setPageLayout(layoutFor(pages[i].scene));
                if (!pdf.newPage()) return false;
            }
            const QSizeF page = pdf.pageLayout().fullRect(QPageLayout::Point).size();
            paintPage(p, page, pages[i], project_name, static_cast<int>(i) + 1, static_cast<int>(pages.size()));
        }
        return p.end();
    }

    bool writeImage(const QString& path, QGraphicsScene* scene) {
        if (!scene || scene->items().isEmpty()) return false;
        const QRectF source = sourceRect(scene);
        // Opaque white background: no alpha channel to store.
        QImage image((source.size() * 2.0).toSize(), QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        scene->render(&painter, QRectF(image.rect()), source);
        painter.end();
        // "Quality" means JPEG quality; for PNG a high value would mean almost
        // no compression, so it keeps Qt's default there.
        const QString suffix = QFileInfo(path).suffix().toLower();
        const bool jpeg = suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg");
        return image.save(path, nullptr, jpeg ? 95 : -1);
    }

    QString safeFileName(const QString& name) {
        QString out = name;
        out.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|\x00-\x1F])")), QStringLiteral("-"));
        out = out.trimmed();
        while (out.endsWith('.')) out.chop(1); // Windows drops trailing dots
        return out.isEmpty() ? QStringLiteral("Esquema") : out;
    }

} // namespace ui::exporting
