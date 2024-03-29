// Copyright 2020 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QDesktopWidget>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrentRun>
#include "core/file_sys/data/data_container.h"
#include "core/file_sys/data/extdata.h"
#include "core/file_sys/data/savegame.h"
#include "core/file_sys/ncch_container.h"
#include "core/key/key.h"
#include "core/sdmc_decryptor.h"
#include "frontend/helpers/rate_limited_progress_dialog.h"
#include "frontend/select_files_dialog.h"
#include "frontend/utilities.h"
#include "ui_utilities.h"

UtilitiesDialog::UtilitiesDialog(QWidget* parent)
    : DPIAwareDialog(parent, 640, 384), ui(std::make_unique<Ui::UtilitiesDialog>()) {

    ui->setupUi(this);

    connect(ui->useSdDecryption, &QCheckBox::clicked, [this] {
        const bool checked = ui->useSdDecryption->isChecked();

        ui->boot9Path->setEnabled(checked);
        ui->boot9PathExplore->setEnabled(checked);
        ui->movableSedPath->setEnabled(checked);
        ui->movableSedPathExplore->setEnabled(checked);
        ui->sdmcPath->setEnabled(checked);
        ui->sdmcPathExplore->setEnabled(checked);

        // First hide both, to avoid resizing the dialog
        ui->sdDecryptionLabel->setVisible(false);
        ui->sdDecryptionDisabledLabel->setVisible(false);
        ui->sdDecryptionLabel->setVisible(checked);
        ui->sdDecryptionDisabledLabel->setVisible(!checked);
        ui->sdDecryption->setEnabled(checked);

        ui->extdataExtractionLabel->setVisible(false);
        ui->extdataExtractionDisabledLabel->setVisible(false);
        ui->extdataExtractionLabel->setVisible(checked);
        ui->extdataExtractionDisabledLabel->setVisible(!checked);
        ui->extdataExtraction->setEnabled(checked);

        ui->romfsExtractionLabel->setVisible(false);
        ui->romfsExtractionDisabledLabel->setVisible(false);
        ui->romfsExtractionLabel->setVisible(!checked);
        ui->romfsExtractionDisabledLabel->setVisible(checked);
        ui->romfsExtraction->setEnabled(!checked);
    });

    connect(ui->boot9PathExplore, &QToolButton::clicked, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Select File"));
        if (!path.isEmpty()) {
            ui->boot9Path->setText(path);
        }
    });
    connect(ui->movableSedPathExplore, &QToolButton::clicked, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Select File"));
        if (!path.isEmpty()) {
            ui->movableSedPath->setText(path);
        }
    });
    connect(ui->sdmcPathExplore, &QToolButton::clicked, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, tr("Select Directory"));
        if (!path.isEmpty()) {
            ui->sdmcPath->setText(path);
        }
    });

    connect(ui->sdDecryption, &QPushButton::clicked, this, &UtilitiesDialog::SDDecryptionTool);
    connect(ui->savedataExtraction, &QPushButton::clicked, this,
            &UtilitiesDialog::SaveDataExtractionTool);
    connect(ui->extdataExtraction, &QPushButton::clicked, this,
            &UtilitiesDialog::ExtdataExtractionTool);
    connect(ui->romfsExtraction, &QPushButton::clicked, this,
            &UtilitiesDialog::RomFSExtractionTool);
}

UtilitiesDialog::~UtilitiesDialog() = default;

std::pair<QString, QString> UtilitiesDialog::GetFilePaths(bool source_is_dir,
                                                          bool destination_is_dir) {

    SelectFilesDialog dialog(this, source_is_dir, destination_is_dir);
    if (dialog.exec() == QDialog::Accepted) {
        return dialog.GetResults();
    } else {
        return {};
    }
}

bool UtilitiesDialog::LoadSDKeys() {
    if (ui->boot9Path->text().isEmpty() || ui->movableSedPath->text().isEmpty()) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Please select boot9.bin and movable.sed paths."));
        return false;
    }
    if (ui->sdmcPath->text().isEmpty()) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Please select SDMC root (\"Nintendo 3DS/&lt;ID0>/&lt;ID1>\")."));
        return false;
    }

    Core::Key::ClearKeys();
    Core::Key::LoadBootromKeys(ui->boot9Path->text().toStdString());
    Core::Key::LoadMovableSedKeys(ui->movableSedPath->text().toStdString());

    if (!Core::Key::IsNormalKeyAvailable(Core::Key::SDKey)) {
        LOG_ERROR(Core, "SDKey is not available");
        QMessageBox::critical(this, tr("Error"),
                              tr("Could not load SD Key. Please check your files."));
        return false;
    }
    return true;
}

void UtilitiesDialog::ShowProgressDialog(std::function<bool()> operation) {
    auto* dialog = new RateLimitedProgressDialog(tr("Processing..."), tr("Cancel"), 0, 0, this);
    dialog->setCancelButton(nullptr);

    using FutureWatcher = QFutureWatcher<void>;
    auto* future_watcher = new FutureWatcher(this);
    connect(future_watcher, &FutureWatcher::finished, this, [this, dialog] {
        dialog->hide();
        ShowResult();
    });

    auto future = QtConcurrent::run([operation, this] { result = operation(); });
    future_watcher->setFuture(future);
}

std::tuple<bool, std::string, std::string> UtilitiesDialog::GetSDMCRoot(const QString& source) {
    QString sdmc_root = ui->sdmcPath->text().replace(QLatin1Char{'\\'}, QLatin1Char{'/'});
    if (!sdmc_root.endsWith(QLatin1Char{'/'})) {
        sdmc_root.append(QLatin1Char{'/'});
    }
    if (!source.startsWith(sdmc_root)) {
        QMessageBox::critical(this, tr("Error"), tr("The file selected is not in SDMC root."));
        return {false, "", ""};
    }
    const std::string relative_source =
        source.toStdString().substr(sdmc_root.toStdString().size() - 1);

    return {true, sdmc_root.toStdString(), relative_source};
}

void UtilitiesDialog::SDDecryptionTool() {
    if (!LoadSDKeys()) {
        return;
    }
    const auto& [source, destination] = GetFilePaths(false, false);
    if (source.isEmpty() || destination.isEmpty()) {
        return;
    }

    const auto& [success, sdmc_root, relative_source] = GetSDMCRoot(source);
    if (!success) {
        return;
    }
    // TODO: Add Progress reporting
    ShowProgressDialog(
        [sdmc_root = sdmc_root, relative_source = relative_source, destination = destination] {
            Core::SDMCDecryptor decryptor(sdmc_root);
            return decryptor.DecryptAndWriteFile(relative_source, destination.toStdString());
        });
}

void UtilitiesDialog::SaveDataExtractionTool() {
    const bool decryption = ui->useSdDecryption->isChecked();
    if (decryption && !LoadSDKeys()) {
        return;
    }
    const auto& [source, destination] = GetFilePaths(false, true);
    if (source.isEmpty() || destination.isEmpty()) {
        return;
    }

    if (decryption) {
        const auto& [success, sdmc_root, relative_source] = GetSDMCRoot(source);
        if (!success) {
            return;
        }

        // TODO: Add Progress reporting
        ShowProgressDialog([sdmc_root = sdmc_root, relative_source = relative_source,
                            source = source, destination = destination] {
            Core::SDMCFile file(sdmc_root, relative_source, "rb");
            Core::Savegame save(file.GetData());
            if (!save.IsGood()) {
                return false;
            }

            return save.Extract(destination.toStdString());
        });
    } else {
        // TODO: Add Progress reporting
        ShowProgressDialog([source = source, destination = destination] {
            FileUtil::IOFile file(source.toStdString(), "rb");
            Core::Savegame save(file.GetData());
            if (!save.IsGood()) {
                return false;
            }

            return save.Extract(destination.toStdString());
        });
    }
}

void UtilitiesDialog::ExtdataExtractionTool() {
    if (!LoadSDKeys()) {
        return;
    }
    const auto& [source, destination] = GetFilePaths(true, true);
    if (source.isEmpty() || destination.isEmpty()) {
        return;
    }

    const auto& [success, sdmc_root, relative_source] = GetSDMCRoot(source);
    if (!success) {
        return;
    }
    // TODO: Add Progress reporting
    ShowProgressDialog(
        [sdmc_root = sdmc_root, relative_source = relative_source, destination = destination] {
            Core::SDMCDecryptor decryptor(sdmc_root);
            Core::Extdata extdata(relative_source, decryptor);
            if (!extdata.IsGood()) {
                return false;
            }

            return extdata.Extract(destination.toStdString());
        });
}

void UtilitiesDialog::RomFSExtractionTool() {
    const auto& [source, destination] = GetFilePaths(false, false);
    if (source.isEmpty() || destination.isEmpty()) {
        return;
    }

    ShowProgressDialog([source = source, destination = destination] {
        FileUtil::IOFile src_file(source.toStdString(), "rb");
        const auto& shared_romfs = Core::LoadSharedRomFS(src_file.GetData());
        if (shared_romfs.empty()) {
            return false;
        }
        return FileUtil::WriteBytesToFile(destination.toStdString(), shared_romfs.data(),
                                          shared_romfs.size());
    });
}

void UtilitiesDialog::ShowResult() {
    if (result) {
        QMessageBox::information(this, tr("Success"), tr("Operation completed successfully."));
    } else {
        QMessageBox message_box(QMessageBox::Critical, tr("threeSD"),
                                tr("Operation failed. Refer to the log for details."),
                                QMessageBox::Ok, this);
        message_box.setDetailedText(QString::fromStdString(Common::Logging::GetLastErrors()));
        message_box.exec();
    }
}
