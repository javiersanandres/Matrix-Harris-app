#pragma once

#include <QCoreApplication>
#include <QString>

class QLocale;
class QSettings;

// Every text the user reads is written in Spanish as Tr::tr("…") (one context,
// "Tr", for the whole interface) and looked up in the English translation,
// app/translations/matrix_harris_en.ts, when the app is in English. The logic's
// texts come through LogicText (see Text.h). Kept outside any namespace so
// lupdate always gives it the same context.
struct Tr {
    Q_DECLARE_TR_FUNCTIONS(Tr)
};

namespace ui::i18n {

    enum class Language { Spanish, English };

    // The language the app is in now.
    Language language();

    // "es" / "en".
    QString code(Language language);

    // The language for a first run on a system in that locale: Spanish for
    // Spanish and the languages close to it (Catalan, Galician, Basque,
    // Asturian, Portuguese, Italian, Romanian), English for any other.
    Language defaultFor(const QLocale& system);

    // The language chosen last, or the system's default before any choice.
    Language saved(QSettings& settings);
    void save(QSettings& settings, Language language);

    // Puts the app in that language: its own texts, Qt's (dialog buttons,
    // context menus…), the logic's and the number format. Windows built
    // afterwards show it; those already open keep their texts.
    void apply(Language language);

    // The picture at that path in the current language (":/en/information/
    // fuse/01.png" for "information/fuse/01.png"), or the Spanish one when
    // there is no picture in that language.
    QString picture(const QString& path);

} // namespace ui::i18n
