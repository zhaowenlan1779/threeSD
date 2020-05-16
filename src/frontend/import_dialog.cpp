// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cmath>
#include <unordered_map>
#include <QCheckBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "frontend/helpers/import_job.h"
#include "frontend/helpers/progressive_job.h"
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

// content type, name, icon name
static constexpr std::array<std::tuple<Core::ContentType, const char*, const char*>, 8>
    ContentTypeMap{{
        {Core::ContentType::Application, QT_TR_NOOP("Application"), "app"},
        {Core::ContentType::Update, QT_TR_NOOP("Update"), "update"},
        {Core::ContentType::DLC, QT_TR_NOOP("DLC"), "dlc"},
        {Core::ContentType::Savegame, QT_TR_NOOP("Save Data"), "save_data"},
        {Core::ContentType::Extdata, QT_TR_NOOP("Extra Data"), "save_data"},
        {Core::ContentType::SystemArchive, QT_TR_NOOP("System Archive"), "system_archive"},
        {Core::ContentType::Sysdata, QT_TR_NOOP("System Data"), "system_data"},
        {Core::ContentType::SystemTitle, QT_TR_NOOP("System Title"), "hos"},
    }};

static const std::unordered_map<Core::EncryptionType, const char*> EncryptionTypeMap{{
    {Core::EncryptionType::None, QT_TR_NOOP("None")},
    {Core::EncryptionType::FixedKey, QT_TR_NOOP("FixedKey")},
    {Core::EncryptionType::NCCHSecure1, QT_TR_NOOP("Secure1")},
    {Core::EncryptionType::NCCHSecure2, QT_TR_NOOP("Secure2")},
    {Core::EncryptionType::NCCHSecure3, QT_TR_NOOP("Secure3")},
    {Core::EncryptionType::NCCHSecure4, QT_TR_NOOP("Secure4")},
}};

QString GetContentName(const Core::ContentSpecifier& specifier) {
    return specifier.name.empty()
               ? QStringLiteral("0x%1").arg(specifier.id, 16, 16, QLatin1Char('0'))
               : QString::fromStdString(specifier.name);
}

QString GetContentTypeName(Core::ContentType type) {
    return QObject::tr(std::get<1>(ContentTypeMap.at(static_cast<std::size_t>(type))),
                       "ImportDialog");
}

QPixmap GetContentTypeIcon(Core::ContentType type) {
    return QIcon::fromTheme(
               QString::fromUtf8(std::get<2>(ContentTypeMap.at(static_cast<std::size_t>(type)))))
        .pixmap(24);
}

QPixmap GetContentIcon(const Core::ContentSpecifier& specifier, bool use_category_icon = false) {
    if (specifier.icon.empty()) {
        // Return a category icon, or a null icon
        return use_category_icon ? GetContentTypeIcon(specifier.type)
                                 : QIcon::fromTheme(QStringLiteral("unknown")).pixmap(24);
    }
    return QPixmap::fromImage(QImage(reinterpret_cast<const uchar*>(specifier.icon.data()), 24, 24,
                                     QImage::Format::Format_RGB16));
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

    ui->title_view_button->setChecked(true);

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

    connect(ui->title_view_button, &QRadioButton::toggled, this, &ImportDialog::RepopulateContent);

    RelistContent();
    UpdateSizeDisplay();

    // Set up column widths.
    // These values are tweaked with regard to the default dialog size.
    ui->main->setColumnWidth(0, width() * 0.11);
    ui->main->setColumnWidth(1, width() * 0.415);
    ui->main->setColumnWidth(2, width() * 0.14);
    ui->main->setColumnWidth(3, width() * 0.17);
    ui->main->setColumnWidth(4, width() * 0.08);

    connect(ui->main, &QTreeWidget::customContextMenuRequested, this, &ImportDialog::OnContextMenu);
}

ImportDialog::~ImportDialog() = default;

void ImportDialog::RelistContent() {
    auto* dialog = new QProgressDialog(tr("Loading Contents..."), tr("Cancel"), 0, 0, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setCancelButton(nullptr);
    dialog->setMinimumDuration(0);
    dialog->setValue(0);

    using FutureWatcher = QFutureWatcher<void>;
    auto* future_watcher = new FutureWatcher(this);
    connect(future_watcher, &FutureWatcher::finished, this, [this, dialog] {
        dialog->hide();
        RepopulateContent();
    });

    auto future = QtConcurrent::run([& contents = this->contents, &importer = this->importer] {
        contents = importer.ListContent();
    });
    future_watcher->setFuture(future);
}

void ImportDialog::InsertTopLevelItem(const QString& text, QPixmap icon) {
    auto* checkBox = new QCheckBox();
    checkBox->setText(text);
    if (!icon.isNull()) {
        checkBox->setIcon(QIcon(icon));
    }
    checkBox->setStyleSheet(QStringLiteral("margin-left: 7px; icon-size: 24px"));
    checkBox->setTristate(true);
    checkBox->setProperty("previousState", static_cast<int>(Qt::Unchecked));

    auto* item = new QTreeWidgetItem;
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
    item->setFirstColumnSpanned(true);
}

void ImportDialog::InsertSecondLevelItem(std::size_t row, const Core::ContentSpecifier& content,
                                         std::size_t id, QString replace_name,
                                         QPixmap replace_icon) {
    auto* checkBox = new QCheckBox();
    checkBox->setStyleSheet(QStringLiteral("margin-left:7px"));
    // HACK: The checkbox is used to record ID. Is there a better way?
    checkBox->setProperty("id", static_cast<unsigned long long>(id));

    const bool use_title_view = ui->title_view_button->isChecked();

    QString name;
    if (use_title_view) {
        if (row == 0) {
            name = QStringLiteral("%1 (%2)")
                       .arg(GetContentName(content))
                       .arg(GetContentTypeName(content.type));
        } else if (row <= 3) {
            name = GetContentName(content);
        } else {
            name = GetContentTypeName(content.type);
        }
    } else {
        name = GetContentName(content);
    }

    if (!replace_name.isEmpty()) {
        name = replace_name;
    }

    QString encryption = tr(EncryptionTypeMap.at(content.encryption));
    if (content.seed_crypto) {
        encryption.append(tr(" (Seed)"));
    }

    if (content.type != Core::ContentType::Application &&
        content.type != Core::ContentType::Update && content.type != Core::ContentType::DLC &&
        content.type != Core::ContentType::SystemTitle) {

        // Do not display encryption in this case
        encryption.clear();
    }

    auto* item = new QTreeWidgetItem{
        {QString{}, name, ReadableByteSize(content.maximum_size), encryption,
         content.already_exists ? QStringLiteral("Yes") : QStringLiteral("No")}};

    QPixmap icon;
    if (replace_icon.isNull()) {
        // Exclude system titles, they are a single group but have own icons.
        if (use_title_view && content.type != Core::ContentType::SystemTitle) {
            icon = GetContentTypeIcon(content.type);
        } else {
            // When not in title view, System Data and System Archive groups use category icons.
            const bool use_category_icon = content.type == Core::ContentType::Sysdata ||
                                           content.type == Core::ContentType::SystemArchive;
            icon = GetContentIcon(content, use_category_icon);
        }
    } else {
        icon = replace_icon;
    }
    item->setData(1, Qt::DecorationRole, icon);

    ui->main->invisibleRootItem()->child(row)->addChild(item);
    ui->main->setItemWidget(item, 0, checkBox);

    connect(checkBox, &QCheckBox::stateChanged,
            [this, item, size = content.maximum_size, type = content.type,
             exists = content.already_exists](int state) {
                if (state == Qt::Checked) {
                    total_size += size;
                } else {
                    if (!warning_shown && !exists &&
                        (type == Core::ContentType::SystemArchive ||
                         type == Core::ContentType::Sysdata ||
                         type == Core::ContentType::SystemTitle)) {

                        QMessageBox::warning(
                            this, tr("Warning"),
                            tr("You are de-selecting important files that may be necessary for "
                               "your imported games to run.\nIt is highly recommended to import "
                               "these contents if they do not exist yet."));
                        warning_shown = true;
                    }
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

void ImportDialog::RepopulateContent() {
    total_size = 0;
    ui->main->clear();
    ui->main->setSortingEnabled(false);

    std::map<u64, QString> title_name_map;       // title ID -> title name
    std::map<u64, QPixmap> title_icon_map;       // title ID -> title icon
    std::unordered_map<u64, u64> extdata_id_map; // extdata ID -> title ID
    for (const auto& content : contents) {
        if (content.type == Core::ContentType::Application) {
            title_name_map.emplace(content.id, GetContentName(content));
            title_icon_map.emplace(content.id, GetContentIcon(content));
            extdata_id_map.emplace(content.extdata_id, content.id);
        }
    }

    const bool use_title_view = ui->title_view_button->isChecked();
    if (use_title_view) {
        title_name_map.insert_or_assign(0, tr("Ungrouped"));
        title_name_map.insert_or_assign(1, tr("System Archive"));
        title_name_map.insert_or_assign(2, tr("System Data"));
        title_name_map.insert_or_assign(3, tr("System Title"));

        title_icon_map.insert_or_assign(0, QIcon::fromTheme(QStringLiteral("unknown")).pixmap(24));
        title_icon_map.insert_or_assign(1, GetContentTypeIcon(Core::ContentType::SystemArchive));
        title_icon_map.insert_or_assign(2, GetContentTypeIcon(Core::ContentType::Sysdata));
        title_icon_map.insert_or_assign(3, GetContentTypeIcon(Core::ContentType::SystemTitle));

        std::unordered_map<u64, u64> title_row_map;
        for (const auto& [id, name] : title_name_map) {
            InsertTopLevelItem(name, title_icon_map.count(id) ? title_icon_map.at(id) : QPixmap{});
            title_row_map[id] = ui->main->invisibleRootItem()->childCount() - 1;
        }

        for (std::size_t i = 0; i < contents.size(); ++i) {
            const auto& content = contents[i];

            std::size_t row = title_row_map.at(0);
            switch (content.type) {
            case Core::ContentType::Application:
            case Core::ContentType::Update:
            case Core::ContentType::DLC:
            case Core::ContentType::Savegame: {
                // Fix the id
                const auto real_id = content.id & 0xffffff00ffffffff;
                row =
                    title_row_map.count(real_id) ? title_row_map.at(real_id) : title_row_map.at(0);
                break;
            }
            case Core::ContentType::Extdata: {
                const auto real_id =
                    extdata_id_map.count(content.id) ? extdata_id_map.at(content.id) : 0;
                row = title_row_map.at(real_id);
                break;
            }
            case Core::ContentType::SystemArchive: {
                row = title_row_map.at(1); // System archive
                break;
            }
            case Core::ContentType::Sysdata: {
                row = title_row_map.at(2); // System data
                break;
            }
            case Core::ContentType::SystemTitle: {
                row = title_row_map.at(3); // System title
                break;
            }
            }

            InsertSecondLevelItem(row, content, i);
        }
    } else {
        for (const auto& [type, name, _] : ContentTypeMap) {
            InsertTopLevelItem(tr(name), GetContentTypeIcon(type));
        }

        for (std::size_t i = 0; i < contents.size(); ++i) {
            const auto& content = contents[i];

            QString name;
            QPixmap icon;
            if (content.type == Core::ContentType::Savegame) {
                name = title_name_map.count(content.id) ? title_name_map.at(content.id) : QString{};
                icon = title_icon_map.count(content.id) ? title_icon_map.at(content.id) : QPixmap{};
            } else if (content.type == Core::ContentType::Extdata) {
                if (extdata_id_map.count(content.id)) {
                    u64 title_id = extdata_id_map.at(content.id);
                    name = title_name_map.count(title_id) ? title_name_map.at(title_id) : QString{};
                    icon = title_icon_map.count(title_id) ? title_icon_map.at(title_id) : QPixmap{};
                }
            }

            InsertSecondLevelItem(static_cast<std::size_t>(content.type), content, i, name, icon);
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
            tr("An error occured while trying to get available space for the storage."));
        reject();
    }

    ui->availableSpace->setText(
        tr("Available Space: %1").arg(ReadableByteSize(storage.bytesAvailable())));
    ui->totalSize->setText(tr("Total Size: %1").arg(ReadableByteSize(total_size)));

    ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)
        ->setEnabled(total_size > 0 && total_size <= static_cast<u64>(storage.bytesAvailable()));
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
                                         .arg(GetContentTypeName(next_content.type)));
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
                                         .arg(GetContentTypeName(current_content.type))
                                         .arg(ReadableByteSize(current_size_imported))
                                         .arg(ReadableByteSize(current_content.maximum_size)));
            });
    connect(job, &ImportJob::Completed, this, [this, dialog, job] {
        dialog->setValue(dialog->maximum());

        const auto failed_contents = job->GetFailedContents();
        if (failed_contents.empty()) {
            QMessageBox::information(this, tr("Import Completed"),
                                     tr("Successfully imported the selected contents."));
        } else {
            QString list_content;
            for (const auto& content : failed_contents) {
                list_content.append(QStringLiteral("<li>%1 (%2)</li>")
                                        .arg(GetContentName(content))
                                        .arg(GetContentTypeName(content.type)));
            }
            QMessageBox::critical(
                this, tr("Import Failed"),
                tr("The following contents couldn't be imported:<ul>%1</ul>").arg(list_content));
        }

        RelistContent();
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

Core::ContentSpecifier ImportDialog::SpecifierFromItem(QTreeWidgetItem* item) const {
    const auto* checkBox = static_cast<QCheckBox*>(ui->main->itemWidget(item, 0));
    return contents[checkBox->property("id").toInt()];
}

void ImportDialog::OnContextMenu(const QPoint& point) {
    QTreeWidgetItem* item = ui->main->itemAt(point.x(), point.y());
    if (!item) {
        return;
    }

    const bool title_view = ui->title_view_button->isChecked();

    QMenu context_menu;
    if (item->parent()) { // Second level
        const auto& specifier = SpecifierFromItem(item);
        if (specifier.type != Core::ContentType::Application) {
            return;
        }

        QAction* dump_cxi = context_menu.addAction(tr("Dump CXI file"));
        connect(dump_cxi, &QAction::triggered, [this, specifier] { StartDumpingCXI(specifier); });
    } else { // Top level
        if (!title_view) {
            return;
        }

        for (int i = 0; i < item->childCount(); ++i) {
            const auto& specifier = SpecifierFromItem(item->child(i));
            if (specifier.type == Core::ContentType::Application) {
                QAction* dump_base_cxi = context_menu.addAction(tr("Dump Base CXI file"));
                connect(dump_base_cxi, &QAction::triggered,
                        [this, specifier] { StartDumpingCXI(specifier); });
                break;
            }
            // TODO: Add updates, etc
        }
    }
    context_menu.exec(ui->main->viewport()->mapToGlobal(point));
}

void ImportDialog::StartDumpingCXI(const Core::ContentSpecifier& specifier) {
    const QString path = QFileDialog::getSaveFileName(this, tr("Dump CXI file"), last_dump_cxi_path,
                                                      tr("CTR Executable Image (*.CXI)"));
    if (path.isEmpty()) {
        return;
    }
    last_dump_cxi_path = QFileInfo(path).path();

    // Try to map total_size to int range
    // This is equal to ceil(total_size / INT_MAX)
    const u64 multiplier = (specifier.maximum_size + std::numeric_limits<int>::max() - 1) /
                           std::numeric_limits<int>::max();

    auto* dialog = new QProgressDialog(tr("Initializing..."), tr("Cancel"), 0,
                                       static_cast<int>(specifier.maximum_size / multiplier), this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setValue(0);

    auto* job = new ProgressiveJob(
        this,
        [this, specifier, path](const ProgressiveJob::ProgressCallback& callback) {
            if (!importer.DumpCXI(specifier, path.toStdString(), callback)) {
                FileUtil::Delete(path.toStdString());
                return false;
            }
            return true;
        },
        [this] { importer.AbortDumpCXI(); });

    connect(job, &ProgressiveJob::ProgressUpdated, this,
            [this, specifier, dialog, multiplier](u64 current, u64 total) {
                dialog->setValue(static_cast<int>(current / multiplier));
                dialog->setLabelText(tr("%1 / %2")
                                         .arg(ReadableByteSize(current))
                                         .arg(ReadableByteSize(specifier.maximum_size)));
            });
    connect(job, &ProgressiveJob::ErrorOccured, this, [this, dialog] {
        QMessageBox::critical(this, tr("threeSD"), tr("Failed to dump CXI!"));
        dialog->hide();
    });
    connect(job, &ProgressiveJob::Completed, this,
            [this, dialog] { dialog->setValue(dialog->maximum()); });
    connect(dialog, &QProgressDialog::canceled, this, [this, job] { job->Cancel(); });

    job->start();
}
