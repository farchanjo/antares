#include "adapters/ui/edit_dialog.hpp"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace antares::adapters::ui {

EditDialog::EditDialog(const QString &title, const QString &body, QWidget *parent)
    : QDialog(parent)
    , editor_(new QPlainTextEdit{body, this})
{
    setWindowTitle(title);
    resize(720, 520);

    auto *layout = new QVBoxLayout{this};
    editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(editor_);

    auto *buttons =
        new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this};
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString EditDialog::body() const { return editor_->toPlainText(); }

} // namespace antares::adapters::ui
