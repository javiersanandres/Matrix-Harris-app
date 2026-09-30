#pragma once

#include <QDialog>
#include <QPoint>
#include <QString>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace ui {

    // ============================================================================
    // StyledDialog
    //
    // The application's own dialog frame, used for every message, question and
    // small informational window instead of the plain QMessageBox:
    //
    //   ┌──────────────────────────────────────────────┐
    //   │  (badge)  Bold title                         │
    //   │           Message text, wrapped              │
    //   │           [optional body widget]             │
    //   │                          [Secondary] [Main]  │
    //   └──────────────────────────────────────────────┘
    //
    // A frameless rounded card with a soft shadow, in the palette of the node
    // dialogs. The badge tells errors, warnings, questions and information
    // apart at a glance. It can be dragged by any bare part of the card, Enter
    // presses the default button and Esc the escape one.
    // ============================================================================
    class StyledDialog : public QDialog {
        Q_OBJECT
    public:
        enum class Badge { Error, Warning, Question, Info, Success, App, Joint };
        enum class ButtonStyle { Primary, Secondary, Danger };

        StyledDialog(Badge badge, const QString& title, QWidget* parent = nullptr);

        // Wrapped text under the title (plain text; line breaks are kept).
        void setMessage(const QString& text);

        // Any widget, placed under the message.
        void setBody(QWidget* body);

        // Adds a button (left to right). Clicking it closes the dialog with
        // choice() == result. The default button answers Enter; the escape
        // button's result is what Esc (or closing) gives.
        QPushButton* addButton(const QString& text, int result, ButtonStyle style,
            bool is_default = false, bool is_escape = false);

        // A button styled like the others that does NOT close the dialog: the
        // caller connects it (e.g. "Pausar" while something runs).
        QPushButton* addActionButton(const QString& text, ButtonStyle style);

        // Replaces the badge with another emblem of the same size (48 x 48),
        // e.g. a busy spinner.
        void setEmblem(QWidget* emblem);

        int choice() const { return choice_; }

        // Sizes and centres the dialog before its window is shown, so Windows
        // creates it with its final geometry (see AppDialogs.cpp).
        void setVisible(bool visible) override;

    protected:
        void keyPressEvent(QKeyEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;

    private:
        QPushButton* makeButton(const QString& text, ButtonStyle style);

        QHBoxLayout* top_;
        QWidget* emblem_;
        QVBoxLayout* text_column_;
        QLabel* message_ = nullptr;
        QWidget* buttons_host_; // hidden until the first button
        QHBoxLayout* buttons_;
        int choice_ = -1;
        int escape_result_ = -1;
        bool dragging_ = false;
        QPoint drag_offset_;
    };

    // ============================================================================
    // Ready-made dialogs built on StyledDialog.
    // ============================================================================
    namespace dialogs {

        // Something went wrong: a single "Entendido" button.
        void showError(QWidget* parent, const QString& title, const QString& text);
        void showWarning(QWidget* parent, const QString& title, const QString& text);
        void showInfo(QWidget* parent, const QString& title, const QString& text);

        // Asks for confirmation. Returns true when the user accepts. A
        // destructive action gets a red button.
        bool confirm(QWidget* parent, const QString& title, const QString& text,
            const QString& accept_text, bool destructive = false);

        // "Guardar / No guardar / Cancelar" before throwing away changes.
        enum class SaveChoice { Save, Discard, Cancel };
        SaveChoice askToSave(QWidget* parent, const QString& text);

    } // namespace dialogs

} // namespace ui
