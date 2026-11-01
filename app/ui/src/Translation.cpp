#include "Translation.h"

#include "Text.h"

#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace ui::i18n {

    namespace {
        const QString LANGUAGE_KEY = QStringLiteral("language");

        Language current = Language::Spanish;

        QTranslator& appTranslator() {
            static QTranslator translator;
            return translator;
        }
        QTranslator& qtTranslator() {
            static QTranslator translator;
            return translator;
        }

        // Qt's own Spanish texts: next to the executable (deployed builds,
        // where windeployqt merges them into qt_es.qm), else in the Qt install.
        bool loadQtSpanish(QTranslator& translator) {
            const QLocale spanish(QLocale::Spanish, QLocale::Spain);
            const QStringList dirs = {
                QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
                QLibraryInfo::path(QLibraryInfo::TranslationsPath) };
            for (const QString& dir : dirs)
                for (const char* catalog : { "qt", "qtbase" })
                    if (translator.load(spanish, QLatin1String(catalog), QStringLiteral("_"), dir)) return true;
            return false;
        }
    } // namespace

    Language language() { return current; }

    QString code(Language language) {
        return language == Language::English ? QStringLiteral("en") : QStringLiteral("es");
    }

    Language defaultFor(const QLocale& system) {
        switch (system.language()) {
        case QLocale::Spanish:
        case QLocale::Catalan:
        case QLocale::Galician:
        case QLocale::Basque:
        case QLocale::Asturian:
        case QLocale::Aragonese:
        case QLocale::Portuguese:
        case QLocale::Italian:
        case QLocale::Romanian:
            return Language::Spanish;
        default:
            return Language::English;
        }
    }

    Language saved(QSettings& settings) {
        const QString value = settings.value(LANGUAGE_KEY).toString();
        if (value == code(Language::English)) return Language::English;
        if (value == code(Language::Spanish)) return Language::Spanish;
        return defaultFor(QLocale::system());
    }

    void save(QSettings& settings, Language language) {
        settings.setValue(LANGUAGE_KEY, code(language));
    }

    void apply(Language language) {
        current = language;
        QCoreApplication::removeTranslator(&appTranslator());
        QCoreApplication::removeTranslator(&qtTranslator());

        // Spanish is the language the texts are written in: only Qt's own need
        // translating. English needs ours (Qt's are English already).
        if (language == Language::English) {
            if (appTranslator().load(QStringLiteral(":/i18n/matrix_harris_en.qm")))
                QCoreApplication::installTranslator(&appTranslator());
            QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        }
        else {
            if (loadQtSpanish(qtTranslator()))
                QCoreApplication::installTranslator(&qtTranslator());
            QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
        }

        hypergraph_logic::setTranslator([](const char* source) {
            return QCoreApplication::translate("LogicText", source).toStdString();
        });
    }

    QString picture(const QString& path) {
        const QString localized = QStringLiteral(":/%1/%2").arg(code(current), path);
        if (current == Language::Spanish || QFile::exists(localized)) return localized;
        return QStringLiteral(":/es/") + path;
    }

} // namespace ui::i18n
