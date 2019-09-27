// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cmath>
#include <unordered_map>
#include <QCheckBox>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "frontend/import_dialog.h"
#include "frontend/import_job.h"
#include "ui_import_dialog.h"

QString ReadableByteSize(qulonglong size) {
    static const std::array<const char*, 6> units = {QT_TR_NOOP("B"),   QT_TR_NOOP("KiB"),
                                                     QT_TR_NOOP("MiB"), QT_TR_NOOP("GiB"),
                                                     QT_TR_NOOP("TiB"), QT_TR_NOOP("PiB")};
    if (size == 0)
        return QStringLiteral("0");
    int digit_groups = std::min<int>(static_cast<int>(std::log10(size) / std::log10(1024)),
                                     static_cast<int>(units.size()));
    return QStringLiteral("%L1 %2")
        .arg(size / std::pow(1024, digit_groups), 0, 'f', 1)
        .arg(QObject::tr(units[digit_groups], "ImportDialog"));
}

static const std::map<Core::ContentType, const char*> ContentTypeMap{
    {Core::ContentType::Application, QT_TR_NOOP("Application")},
    {Core::ContentType::Update, QT_TR_NOOP("Update")},
    {Core::ContentType::DLC, QT_TR_NOOP("DLC")},
    {Core::ContentType::Savegame, QT_TR_NOOP("Save Data")},
    {Core::ContentType::Extdata, QT_TR_NOOP("Extra Data")},
    {Core::ContentType::Sysdata, QT_TR_NOOP("System Data")},
};

QString GetContentName(const Core::ContentSpecifier& specifier) {
    return specifier.name.empty()
               ? QStringLiteral("0x%1").arg(specifier.id, 16, 16, QLatin1Char('0'))
               : QString::fromStdString(specifier.name);
}

ImportDialog::ImportDialog(QWidget* parent, const Core::Config& config)
    : QDialog(parent), ui(std::make_unique<Ui::ImportDialog>()), user_path(config.user_path),
      importer(config) {

    qRegisterMetaType<u64>("u64");
    qRegisterMetaType<Core::ContentSpecifier>();

    ui->setupUi(this);
    if (!importer.IsGood()) {
        QMessageBox::critical(
            this, tr("Importer Error"),
            tr("Failed to initalize the importer.\nRefer to the log for details."));
        reject();
    }

    ui->buttonBox->button(QDialogButtonBox::StandardButton::Reset)->setText(tr("Refresh"));
    connect(ui->buttonBox, &QDialogButtonBox::clicked, [this](QAbstractButton* button) {
        if (button == ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)) {
            StartImporting();
        } else if (button == ui->buttonBox->button(QDialogButtonBox::StandardButton::Cancel)) {
            reject();
        } else {
            RelistContent();
        }
    });

    RelistContent();
    UpdateSizeDisplay();

    // Set up column widths
    ui->main->setColumnWidth(0, width() / 8);
    ui->main->setColumnWidth(1, width() / 2);
    ui->main->setColumnWidth(2, width() / 6);
    ui->main->setColumnWidth(3, width() / 10);
}

ImportDialog::~ImportDialog() = default;

void ImportDialog::RelistContent() {
    auto* dialog = new QProgressDialog(tr("Loading Contents..."), tr("Cancel"), 0, 0, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setCancelButton(nullptr);
    dialog->setMinimumDuration(0);
    dialog->setValue(0);

    using FutureWatcher = QFutureWatcher<std::vector<Core::ContentSpecifier>>;
    auto* future_watcher = new FutureWatcher(this);
    connect(future_watcher, &FutureWatcher::finished, this, [this, dialog] {
        dialog->hide();
        RepopulateContent();
    });

    auto future =
        QtConcurrent::run([& importer = this->importer] { return importer.ListContent(); });
    future_watcher->setFuture(future);
}

void ImportDialog::RepopulateContent() {
    total_size = 0;
    contents = importer.ListContent();
    ui->main->clear();
    ui->main->setSortingEnabled(false);

    std::map<u64, QString> title_name_map;       // title ID -> title name
    std::unordered_map<u64, u64> extdata_id_map; // extdata ID -> title ID
    for (const auto& content : contents) {
        if (content.type == Core::ContentType::Application) {
            title_name_map.emplace(content.id, GetContentName(content));
            extdata_id_map.emplace(content.extdata_id, content.id);
        }
    }
    title_name_map.insert_or_assign(0, tr("Ungrouped"));

    std::unordered_map<u64, u64> title_row_map;
    for (const auto& [id, name] : title_name_map) {
        auto* checkBox = new QCheckBox();
        checkBox->setText(name);
        checkBox->setStyleSheet(QStringLiteral("margin-left:7px"));
        checkBox->setTristate(true);
        checkBox->setProperty("previousState", static_cast<int>(Qt::Unchecked));

        auto* item = new QTreeWidgetItem;
        item->setFirstColumnSpanned(true);
        ui->main->invisibleRootItem()->addChild(item);
        title_row_map[id] = ui->main->invisibleRootItem()->childCount() - 1;

        connect(checkBox, &QCheckBox::stateChanged, [this, checkBox, item](int state) {
            SCOPE_EXIT({ checkBox->setProperty("previousState", state); });

            if (program_trigger) {
                program_trigger = false;
                return;
            }

            if (state == Qt::PartiallyChecked) {
                if (checkBox->property("previousState").toInt() == Qt::Unchecked) {
                    checkBox->setCheckState(static_cast<Qt::CheckState>(state = Qt::Checked));
                } else {
                    checkBox->setCheckState(static_cast<Qt::CheckState>(state = Qt::Unchecked));
                }
                return;
            }

            program_trigger = true;
            for (int i = 0; i < item->childCount(); ++i) {
                static_cast<QCheckBox*>(ui->main->itemWidget(item->child(i), 0))
                    ->setCheckState(static_cast<Qt::CheckState>(state));
            }
            program_trigger = false;
        });

        ui->main->setItemWidget(item, 0, checkBox);
    }

    for (std::size_t i = 0; i < contents.size(); ++i) {
        const auto& content = contents[i];

        auto* checkBox = new QCheckBox();
        checkBox->setStyleSheet(QStringLiteral("margin-left:7px"));
        // HACK: The checkbox is used to record ID. Is there a better way?
        checkBox->setProperty("id", i);

        std::size_t row = title_row_map.at(0);
        switch (content.type) {
        case Core::ContentType::Application:
        case Core::ContentType::Update:
        case Core::ContentType::DLC:
        case Core::ContentType::Savegame: {
            // Fix the id
            const auto real_id = content.id & 0xffffff00ffffffff;
            row = title_row_map.count(real_id) ? title_row_map.at(real_id) : title_row_map.at(0);
            break;
        }
        case Core::ContentType::Extdata: {
            const auto real_id =
                extdata_id_map.count(content.id) ? extdata_id_map.at(content.id) : 0;
            row = title_row_map.at(real_id);
            break;
        }
        case Core::ContentType::Sysdata: {
            row = title_row_map.at(0);
            break;
        }
        }

        const QString name = (row == 0 ? QStringLiteral("%1 (%2)")
                                             .arg(GetContentName(content))
                                             .arg(tr(ContentTypeMap.at(content.type)))
                                       : tr(ContentTypeMap.at(content.type)));

        auto* item = new QTreeWidgetItem{
            {QString{}, name, ReadableByteSize(content.maximum_size),
             content.already_exists ? QStringLiteral("Yes") : QStringLiteral("No")}};

        ui->main->invisibleRootItem()->child(row)->addChild(item);
        ui->main->setItemWidget(item, 0, checkBox);

        connect(checkBox, &QCheckBox::stateChanged,
                [this, item, size = content.maximum_size](int state) {
                    if (state == Qt::Checked) {
                        total_size += size;
                    } else {
                        total_size -= size;
                    }
                    UpdateSizeDisplay();

                    if (!program_trigger) {
                        UpdateItemCheckState(item->parent());
                    }
                });

        if (!content.already_exists) {
            checkBox->setChecked(true);
        }
    }

    ui->main->setSortingEnabled(true);
}

void ImportDialog::UpdateSizeDisplay() {
    QStorageInfo storage(QString::fromStdString(user_path));
    if (!storage.isValid() || !storage.isReady()) {
        LOG_ERROR(Frontend, "Storage {} is not good", user_path);
        QMessageBox::critical(
            this, tr("Bad Storage"),
            tr("An error occured while trying to get available space for the storage.\nPlease "
               "ensure that your SD card is well connected and try again."));
        reject();
    }

    ui->availableSpace->setText(
        tr("Available Space: %1").arg(ReadableByteSize(storage.bytesAvailable())));
    ui->totalSize->setText(tr("Total Size: %1").arg(ReadableByteSize(total_size)));

    ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)
        ->setEnabled(total_size <= static_cast<u64>(storage.bytesAvailable()));
}

void ImportDialog::UpdateItemCheckState(QTreeWidgetItem* item) {
    bool has_checked = false, has_unchecked = false;
    auto* item_checkBox = static_cast<QCheckBox*>(ui->main->itemWidget(item, 0));
    for (int i = 0; i < item->childCount(); ++i) {
        auto* checkBox = static_cast<QCheckBox*>(ui->main->itemWidget(item->child(i), 0));
        if (checkBox->isChecked()) {
            has_checked = true;
        } else {
            has_unchecked = true;
        }
        if (has_checked && has_unchecked) {
            program_trigger = true;
            item_checkBox->setCheckState(Qt::PartiallyChecked);
            program_trigger = false;
            return;
        }
    }
    program_trigger = true;
    if (has_checked) {
        item_checkBox->setCheckState(Qt::Checked);
    } else {
        item_checkBox->setCheckState(Qt::Unchecked);
    }
    program_trigger = false;
}

std::vector<Core::ContentSpecifier> ImportDialog::GetSelectedContentList() {
    std::vector<Core::ContentSpecifier> to_import;
    for (int i = 0; i < ui->main->invisibleRootItem()->childCount(); ++i) {
        const auto* item = ui->main->invisibleRootItem()->child(i);
        for (int j = 0; j < item->childCount(); ++j) {
            const auto* checkBox = static_cast<QCheckBox*>(ui->main->itemWidget(item->child(j), 0));
            if (checkBox->isChecked()) {
                to_import.emplace_back(contents[checkBox->property("id").toInt()]);
            }
        }
    }

    return to_import;
}

void ImportDialog::StartImporting() {
    UpdateSizeDisplay();
    if (!ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)->isEnabled()) {
        // Space is no longer enough
        QMessageBox::warning(this, tr("Not Enough Space"),
                             tr("Your disk does not have enough space to hold imported data."));
        return;
    }

    const auto& to_import = GetSelectedContentList();
    const std::size_t total_count = to_import.size();

    // Try to map total_size to int range
    // This is equal to ceil(total_size / INT_MAX)
    const u64 multiplier =
        (total_size + std::numeric_limits<int>::max() - 1) / std::numeric_limits<int>::max();

    auto* dialog = new QProgressDialog(tr("Initializing..."), tr("Cancel"), 0,
                                       static_cast<int>(total_size / multiplier), this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setValue(0);

    auto* job = new ImportJob(this, importer, std::move(to_import));

    connect(job, &ImportJob::NextContent, this,
            [this, dialog, multiplier, total_count](u64 size_imported, u64 count,
                                                    Core::ContentSpecifier next_content) {
                dialog->setValue(static_cast<int>(size_imported / multiplier));
                dialog->setLabelText(tr("(%1/%2) Importing %3 (%4)...")
                                         .arg(count)
                                         .arg(total_count)
                                         .arg(GetContentName(next_content))
                                         .arg(tr(ContentTypeMap.at(next_content.type))));
                current_content = next_content;
                current_count = count;
            });
    connect(job, &ImportJob::ProgressUpdated, this,
            [this, dialog, multiplier, total_count](u64 total_size_imported,
                                                    u64 current_size_imported) {
                dialog->setValue(static_cast<int>(total_size_imported / multiplier));
                dialog->setLabelText(tr("(%1/%2) Importing %3 (%4) (%5/%6)...")
                                         .arg(current_count)
                                         .arg(total_count)
                                         .arg(GetContentName(current_content))
                                         .arg(tr(ContentTypeMap.at(current_content.type)))
                                         .arg(ReadableByteSize(current_size_imported))
                                         .arg(ReadableByteSize(current_content.maximum_size)));
            });
    connect(job, &ImportJob::ErrorOccured, this,
            [this, dialog](Core::ContentSpecifier current_content) {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Failed to import content %1 (%2)!")
                                          .arg(GetContentName(current_content))
                                          .arg(tr(ContentTypeMap.at(current_content.type))));
                dialog->hide();
            });
    connect(job, &ImportJob::Completed, this, [this, dialog] {
        dialog->setValue(dialog->maximum());
        RepopulateContent();
    });
    connect(dialog, &QProgressDialog::canceled, this, [this, dialog, job] {
        // Add yet-another-ProgressDialog to indicate cancel progress
        auto* cancel_dialog = new QProgressDialog(tr("Canceling..."), tr("Cancel"), 0, 0, this);
        cancel_dialog->setWindowModality(Qt::WindowModal);
        cancel_dialog->setCancelButton(nullptr);
        cancel_dialog->setMinimumDuration(0);
        cancel_dialog->setValue(0);
        connect(job, &ImportJob::Completed, cancel_dialog, &QProgressDialog::hide);
        job->Cancel();
    });

    job->start();
}
