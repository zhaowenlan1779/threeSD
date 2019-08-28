// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>
#include <QStorageInfo>
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "frontend/import_dialog.h"
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

ImportDialog::ImportDialog(QWidget* parent, const Core::Config& config)
    : QDialog(parent), ui(std::make_unique<Ui::ImportDialog>()), user_path(config.user_path),
      importer(config) {

    ui->setupUi(this);
    if (!importer.IsGood()) {
        QMessageBox::critical(
            this, tr("Importer Error"),
            tr("Failed to initalize the importer.\nRefer to the log for details."));
        reject();
    }

    PopulateContent();
    UpdateSizeDisplay();
}

ImportDialog::~ImportDialog() = default;

void ImportDialog::PopulateContent() {
    contents = importer.ListContent();
    ui->main->clear();
    ui->main->setSortingEnabled(false);

    const std::map<Core::ContentType, QString> content_type_map{
        {Core::ContentType::Application, QStringLiteral("Application")},
        {Core::ContentType::Update, QStringLiteral("Update")},
        {Core::ContentType::DLC, QStringLiteral("DLC (Add-on Content)")},
        {Core::ContentType::Savegame, QStringLiteral("Save Data")},
        {Core::ContentType::Extdata, QStringLiteral("Extra Data")},
        {Core::ContentType::Sysdata, QStringLiteral("System Data")},
    };

    for (const auto& [type, name] : content_type_map) {
        auto* checkBox = new QCheckBox();
        checkBox->setText(name);
        checkBox->setStyleSheet(QStringLiteral("margin-left:7px"));
        checkBox->setTristate(true);
        checkBox->setProperty("previousState", static_cast<int>(Qt::Unchecked));

        auto* item = new QTreeWidgetItem;
        item->setFirstColumnSpanned(true);
        ui->main->invisibleRootItem()->addChild(item);

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

    for (const auto& content : contents) {
        auto* checkBox = new QCheckBox();
        checkBox->setStyleSheet(QStringLiteral("margin-left:7px"));

        auto* item = new QTreeWidgetItem{
            {QString{},
             content.name.empty() ? QStringLiteral("0x%1").arg(content.id, 16, 16, QLatin1Char('0'))
                                  : QString::fromStdString(content.name),
             ReadableByteSize(content.maximum_size),
             content.already_exists ? QStringLiteral("Yes") : QStringLiteral("No")}};

        ui->main->invisibleRootItem()->child(static_cast<int>(content.type))->addChild(item);
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
