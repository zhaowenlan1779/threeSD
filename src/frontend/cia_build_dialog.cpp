// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QDesktopWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include "common/assert.h"
#include "frontend/cia_build_dialog.h"
#include "ui_cia_build_dialog.h"

CIABuildDialog::CIABuildDialog(QWidget* parent, bool is_dir_, bool is_nand, bool enable_legit,
                               const QString& default_path)
    : QDialog(parent), ui(std::make_unique<Ui::CIABuildDialog>()), is_dir(is_dir_) {
    ui->setupUi(this);

    const double scale = qApp->desktop()->logicalDpiX() / 96.0;
    resize(static_cast<int>(width() * scale), static_cast<int>(height() * scale));

    if (is_dir) {
        setWindowTitle(tr("Batch Build CIA"));
    }
    if (is_nand) {
        ui->pirateLegitButton->setVisible(false);
        ui->pirateLegitLabel->setVisible(false);

        auto message = tr("Encrypted CIA with legit TMD, encrypted contents and legit ticket.<br>");
        if (is_dir) {
            message.append(tr(
                "Legit tickets for these titles do not include console-identifying information."));
        } else {
            message.append(tr(
                "Legit ticket for this title does not include console-identifying information."));
        }
        ui->legitLabel->setText(message);
    }
    if (!enable_legit) {
        const auto message =
            is_dir ? tr("This option is not available as some of the titles are not legit.")
                   : tr("This option is not available as the title is not legit.");
        ui->pirateLegitButton->setEnabled(false);
        ui->pirateLegitLabel->setText(message);
        ui->legitButton->setEnabled(false);
        ui->legitLabel->setText(message);
    }

    connect(ui->buttonBox, &QDialogButtonBox::accepted, [this] {
        if (ui->destination->text().isEmpty()) {
            const QString message = is_dir ? tr("Please specify destination folder.")
                                           : tr("Please specify destination file.");
            QMessageBox::warning(this, tr("threeSD"), message);
            return;
        }
        accept();
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &CIABuildDialog::reject);

    if (is_dir) {
        ui->destination->setText(default_path);
    }
    connect(ui->destinationExplore, &QToolButton::clicked, [this, default_path] {
        QString path;
        if (is_dir) {
            path = QFileDialog::getExistingDirectory(this, tr("Batch Build CIA"),
                                                     ui->destination->text());
        } else {
            const auto cur = ui->destination->text().isEmpty()
                                 ? default_path
                                 : QFileInfo(ui->destination->text()).path();
            path = QFileDialog::getSaveFileName(this, tr("Build CIA"), cur,
                                                tr("CTR Importable Archive (*.cia)"));
        }
        if (!path.isEmpty()) {
            ui->destination->setText(path);
        }
    });
}

CIABuildDialog::~CIABuildDialog() = default;

std::pair<QString, Core::CIABuildType> CIABuildDialog::GetResults() const {
    Core::CIABuildType type;
    if (ui->standardButton->isChecked()) {
        type = Core::CIABuildType::Standard;
    } else if (ui->pirateLegitButton->isChecked()) {
        type = Core::CIABuildType::PirateLegit;
    } else if (ui->legitButton->isChecked()) {
        type = Core::CIABuildType::Legit;
    } else {
        UNREACHABLE();
    }
    return {ui->destination->text(), type};
}
