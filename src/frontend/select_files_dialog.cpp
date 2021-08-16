// Copyright 2020 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QDesktopWidget>
#include <QFileDialog>
#include <QMessageBox>
#include "frontend/select_files_dialog.h"
#include "ui_select_files_dialog.h"

SelectFilesDialog::SelectFilesDialog(QWidget* parent, bool source_is_dir_, bool destination_is_dir_)
    : DPIAwareDialog(parent, 480, 96), ui(std::make_unique<Ui::SelectFilesDialog>()),
      source_is_dir(source_is_dir_), destination_is_dir(destination_is_dir_) {

    ui->setupUi(this);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));

    connect(ui->buttonBox, &QDialogButtonBox::accepted, [this] {
        if (ui->source->text().isEmpty() || ui->destination->text().isEmpty()) {
            QMessageBox::warning(this, tr("Warning"), tr("Please fill in all the fields."));
            return;
        }
        accept();
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SelectFilesDialog::reject);

    connect(ui->sourceExplore, &QToolButton::clicked, [this] {
        QString path;
        if (source_is_dir) {
            path = QFileDialog::getExistingDirectory(this, tr("Select Directory"));
        } else {
            path = QFileDialog::getOpenFileName(this, tr("Select File"));
        }
        if (!path.isEmpty()) {
            ui->source->setText(path);
        }
    });
    connect(ui->destinationExplore, &QToolButton::clicked, [this] {
        QString path;
        if (destination_is_dir) {
            path = QFileDialog::getExistingDirectory(this, tr("Select Directory"));
        } else {
            path = QFileDialog::getSaveFileName(this, tr("Select File"));
        }
        if (!path.isEmpty()) {
            ui->destination->setText(path);
        }
    });
}

SelectFilesDialog::~SelectFilesDialog() = default;

std::pair<QString, QString> SelectFilesDialog::GetResults() const {
    return {ui->source->text(), ui->destination->text()};
}
