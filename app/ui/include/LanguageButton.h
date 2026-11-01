#pragma once

#include "Translation.h"

#include <QToolButton>

namespace ui {

    // The current language's flag and code ("ES", "EN"). Clicking it offers
    // the languages, each written in itself, so someone who does not read the
    // current one still finds theirs. Choosing another one emits languageChosen.
    class LanguageButton : public QToolButton {
        Q_OBJECT

    public:
        explicit LanguageButton(QWidget* parent = nullptr);

    signals:
        void languageChosen(ui::i18n::Language language);
    };

} // namespace ui
