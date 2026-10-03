#include "PieceClipboard.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

using namespace hypergraph_logic;

namespace ui::clipboard {

    namespace {

        // The clipboard's copy, if it is one this version can read.
        std::optional<json> readPayload() {
            const QMimeData* data = QGuiApplication::clipboard()->mimeData();
            if (!data || !data->hasFormat(QString::fromLatin1(MIME_TYPE))) return std::nullopt;
            const QByteArray bytes = data->data(QString::fromLatin1(MIME_TYPE));
            json payload = json::parse(bytes.constData(), bytes.constData() + bytes.size(), nullptr, false);
            if (payload.is_discarded() || !payload.is_object()) return std::nullopt;
            if (payload.value("format", std::string()) != FORMAT) return std::nullopt;
            if (payload.value("version", 0) != VERSION) return std::nullopt;
            if (!payload.contains("graph") || !payload["graph"].is_object()) return std::nullopt;
            return payload;
        }

        std::optional<GraphicalHypergraph> readGraph(const json& payload) {
            try {
                GraphicalHypergraph g = GraphicalHypergraph::fromJSON(payload["graph"]);
                if (g.getAllNodes().empty()) return std::nullopt;
                return g;
            }
            catch (const std::exception&) {
                return std::nullopt;
            }
        }

    } // namespace

    void copy(const GraphicalHypergraph& piece, const QString& description) {
        int boxes = 0;
        for (const auto& n : piece.getAllNodes()) boxes += !n->isDummy();

        json payload;
        payload["format"] = FORMAT;
        payload["version"] = VERSION;
        payload["description"] = description.toStdString();
        payload["boxes"] = boxes;
        piece.toJSON(payload["graph"]);

        const std::string text = payload.dump();
        auto* data = new QMimeData;
        data->setData(QString::fromLatin1(MIME_TYPE), QByteArray(text.data(), static_cast<qsizetype>(text.size())));
        QGuiApplication::clipboard()->setMimeData(data); // the clipboard owns it
    }

    std::optional<GraphicalHypergraph> piece() {
        const auto payload = readPayload();
        if (!payload) return std::nullopt;
        return readGraph(*payload);
    }

    QString description() {
        const auto payload = readPayload();
        if (!payload || !readGraph(*payload)) return QString();
        const QString what = QString::fromStdString(payload->value("description", std::string()));
        const int boxes = payload->value("boxes", 0);
        const QString count = boxes == 1 ? QStringLiteral("1 caja") : QStringLiteral("%1 cajas").arg(boxes);
        return what.isEmpty() ? count : QStringLiteral("%1 (%2)").arg(what, count);
    }

    bool hasPiece() {
        return piece().has_value();
    }

} // namespace ui::clipboard
