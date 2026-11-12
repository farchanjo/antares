#pragma once

#include <QDialog>

class QPlainTextEdit;

namespace antares::adapters::ui {

/// Edits one object's document.
///
/// The reference opens an editor on the object; this keeps the same idea with a
/// plain text body. The text is a JSON object and is sent as a merge patch, so a
/// field left out keeps its server value.
class EditDialog final : public QDialog {
    Q_OBJECT

public:
    EditDialog(const QString &title, const QString &body, QWidget *parent = nullptr);

    /// The edited body.
    [[nodiscard]] QString body() const;

private:
    QPlainTextEdit *editor_ = nullptr;
};

} // namespace antares::adapters::ui
