// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <QCheckBox>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/progress_callback.h"
#include "common/scope_exit.h"
#include "frontend/cia_build_dialog.h"
#include "frontend/helpers/frontend_common.h"
#include "frontend/helpers/multi_job.h"
#include "frontend/helpers/simple_job.h"
#include "frontend/import_dialog.h"
#include "frontend/title_info_dialog.h"
#include "ui_import_dialog.h"

// content type, singular name, plural name, icon name
// clang-format off
static constexpr std::array<std::tuple<Core::ContentType, const char*, const char*, const char*>, 9>
    ContentTypeMap{{
        {Core::ContentType::Application, QT_TR_NOOP("Application"), QT_TR_NOOP("Applications"), "app"},
        {Core::ContentType::Update, QT_TR_NOOP("Update"),  QT_TR_NOOP("Updates"), "update"},
        {Core::ContentType::DLC, QT_TR_NOOP("DLC"), QT_TR_NOOP("DLCs"), "dlc"},
        {Core::ContentType::Savegame, QT_TR_NOOP("Save Data"), QT_TR_NOOP("Save Data"), "save_data"},
        {Core::ContentType::Extdata, QT_TR_NOOP("Extra Data"), QT_TR_NOOP("Extra Data"), "save_data"},
        {Core::ContentType::SystemArchive, QT_TR_NOOP("System Archive"), QT_TR_NOOP("System Archives"), "system_archive"},
        {Core::ContentType::Sysdata, QT_TR_NOOP("System Data"), QT_TR_NOOP("System Data"), "system_data"},
        {Core::ContentType::SystemTitle, QT_TR_NOOP("System Title"), QT_TR_NOOP("System Titles"), "hos"},
        {Core::ContentType::SystemApplet, QT_TR_NOOP("System Applet"), QT_TR_NOOP("System Applets"), "hos"},
    }};
// clang-format on

static QString GetContentName(const Core::ContentSpecifier& specifier) {
    return specifier.name.empty()
               ? QStringLiteral("0x%1").arg(specifier.id, 16, 16, QLatin1Char('0'))
               : QString::fromStdString(specifier.name);
}

template <bool Plural = true>
static QString GetContentTypeName(Core::ContentType type) {
    if constexpr (Plural) {
        return QObject::tr(std::get<2>(ContentTypeMap.at(static_cast<std::size_t>(type))),
                           "ImportDialog");
    } else {
        return QObject::tr(std::get<1>(ContentTypeMap.at(static_cast<std::size_t>(type))),
                           "ImportDialog");
    }
}

static QPixmap GetContentTypeIcon(Core::ContentType type) {
    return QIcon::fromTheme(
               QString::fromUtf8(std::get<3>(ContentTypeMap.at(static_cast<std::size_t>(type)))))
        .pixmap(24);
}

static QPixmap GetContentIcon(const Core::ContentSpecifier& specifier,
                              bool use_category_icon = false) {
    if (specifier.icon.empty()) {
        // Return a category icon, or a null icon
        return use_category_icon ? GetContentTypeIcon(specifier.type)
                                 : QIcon::fromTheme(QStringLiteral("unknown")).pixmap(24);
    }
    return QPixmap::fromImage(QImage(reinterpret_cast<const uchar*>(specifier.icon.data()), 24, 24,
                                     QImage::Format::Format_RGB16));
}

ImportDialog::ImportDialog(QWidget* parent, const Core::Config& config_)
    : QDialog(parent), ui(std::make_unique<Ui::ImportDialog>()), config(config_) {

    qRegisterMetaType<u64>("u64");
    qRegisterMetaType<std::size_t>("std::size_t");
    qRegisterMetaType<Core::ContentSpecifier>();

    ui->setupUi(this);

    const double scale = qApp->desktop()->logicalDpiX() / 96.0;
    resize(static_cast<int>(width() * scale), static_cast<int>(height() * scale));

    RelistContent();
    UpdateSizeDisplay();

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
    connect(ui->advanced_button, &QPushButton::clicked, this, &ImportDialog::ShowAdvancedMenu);

    // Set up column widths.
    // These values are tweaked with regard to the default dialog size.
    ui->main->setColumnWidth(0, width() * 0.11);
    ui->main->setColumnWidth(1, width() * 0.525);
    ui->main->setColumnWidth(2, width() * 0.18);
    ui->main->setColumnWidth(3, width() * 0.10);

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
        if (importer->IsGood()) {
            RepopulateContent();
        } else {
            QMessageBox::critical(
                this, tr("Importer Error"),
                tr("Failed to initalize the importer.\nRefer to the log for details."));
            reject();
        }
    });

    auto future = QtConcurrent::run(
        [&importer = this->importer, &config = this->config, &contents = this->contents] {
            if (!importer) {
                importer = std::make_unique<Core::SDMCImporter>(config);
            }
            if (importer->IsGood()) {
                contents = importer->ListContent();
            }
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

// Content types that themselves form a 'Title' like entity.
constexpr std::array<Core::ContentType, 4> SpecialContentTypeList{{
    Core::ContentType::SystemArchive,
    Core::ContentType::Sysdata,
    Core::ContentType::SystemTitle,
    Core::ContentType::SystemApplet,
}};

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
                       .arg(GetContentTypeName<false>(content.type));
        } else if (row <= SpecialContentTypeList.size()) {
            name = GetContentName(content);
        } else {
            name = GetContentTypeName<false>(content.type);
        }
    } else {
        name = GetContentName(content);
    }

    if (!replace_name.isEmpty()) {
        name = replace_name;
    }

    auto* item = new QTreeWidgetItem{{QString{}, name, ReadableByteSize(content.maximum_size),
                                      content.already_exists ? tr("Yes") : tr("No")}};

    QPixmap icon;
    if (replace_icon.isNull()) {
        // Exclude system titles, they are a single group but have own icons.
        if (use_title_view && content.type != Core::ContentType::SystemTitle &&
            content.type != Core::ContentType::SystemApplet) {
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
                    if (!applet_warning_shown && !exists &&
                        type == Core::ContentType::SystemApplet) {
                        QMessageBox::warning(
                            this, tr("Warning"),
                            tr("You are trying to import System Applets.\nThese are known to cause "
                               "problems with certain games.\nOnly proceed if you understand what "
                               "you are doing."));
                        applet_warning_shown = true;
                    }
                    total_selected_size += size;
                } else {
                    if (!system_warning_shown && !exists &&
                        (type == Core::ContentType::SystemArchive ||
                         type == Core::ContentType::Sysdata ||
                         type == Core::ContentType::SystemTitle)) {

                        QMessageBox::warning(
                            this, tr("Warning"),
                            tr("You are de-selecting important files that may be necessary for "
                               "your imported games to run.\nIt is highly recommended to import "
                               "these contents if they do not exist yet."));
                        system_warning_shown = true;
                    }
                    total_selected_size -= size;
                }
                UpdateSizeDisplay();

                if (!program_trigger) {
                    UpdateItemCheckState(item->parent());
                }
            });

    // Skip System Applets, but enable everything else by default.
    if (!content.already_exists && content.type != Core::ContentType::SystemApplet) {
        checkBox->setChecked(true);
    }
}

void ImportDialog::RepopulateContent() {
    if (contents.empty()) { // why???
        QMessageBox::warning(this, tr("threeSD"), tr("Sorry, there are no contents available."));
        reject();
        return;
    }

    total_selected_size = 0;
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
        // Create 'Ungrouped' category.
        title_name_map.insert_or_assign(0, tr("Ungrouped"));
        title_icon_map.insert_or_assign(0, QIcon::fromTheme(QStringLiteral("unknown")).pixmap(24));

        // Create categories for special content types.
        for (std::size_t i = 0; i < SpecialContentTypeList.size(); ++i) {
            title_name_map.insert_or_assign(i + 1, GetContentTypeName(SpecialContentTypeList[i]));
            title_icon_map.insert_or_assign(i + 1, GetContentTypeIcon(SpecialContentTypeList[i]));
        }

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
            default: {
                const std::size_t idx = std::find(SpecialContentTypeList.begin(),
                                                  SpecialContentTypeList.end(), content.type) -
                                        SpecialContentTypeList.begin();
                ASSERT_MSG(idx < SpecialContentTypeList.size(), "Content Type not handled");
                row = title_row_map.at(idx + 1);
                break;
            }
            }

            InsertSecondLevelItem(row, content, i);
        }
    } else {
        for (const auto& [type, singular_name, plural_name, icon_name] : ContentTypeMap) {
            InsertTopLevelItem(tr(plural_name), GetContentTypeIcon(type));
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
    QStorageInfo storage(QString::fromStdString(config.user_path));
    if (!storage.isValid() || !storage.isReady()) {
        LOG_ERROR(Frontend, "Storage {} is not good", config.user_path);
        QMessageBox::critical(
            this, tr("Bad Storage"),
            tr("An error occured while trying to get available space for the storage."));
        reject();
        return;
    }

    ui->availableSpace->setText(
        tr("Available Space: %1").arg(ReadableByteSize(storage.bytesAvailable())));
    ui->totalSize->setText(tr("Total Size: %1").arg(ReadableByteSize(total_selected_size)));

    ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)
        ->setEnabled(total_selected_size > 0 &&
                     total_selected_size <= static_cast<u64>(storage.bytesAvailable()));
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
        if (specifier.type == Core::ContentType::Application) {
            QAction* dump_cxi = context_menu.addAction(tr("Dump CXI file"));
            connect(dump_cxi, &QAction::triggered,
                    [this, specifier] { StartDumpingCXISingle(specifier); });
        }
        if (Core::IsTitle(specifier.type)) {
            QAction* build_cia = context_menu.addAction(tr("Build CIA..."));
            connect(build_cia, &QAction::triggered,
                    [this, specifier] { StartBuildingCIASingle(specifier); });
            QAction* show_title_info = context_menu.addAction(tr("Show Title Info"));
            connect(show_title_info, &QAction::triggered, [this, specifier] {
                TitleInfoDialog dialog(this, config, *importer, specifier);
                dialog.exec();
            });
        }
    } else { // Top level
        if (!title_view) {
            return;
        }

        for (int i = 0; i < item->childCount(); ++i) {
            const auto& specifier = SpecifierFromItem(item->child(i));
            if (specifier.type == Core::ContentType::Application) {
                QAction* dump_base_cxi = context_menu.addAction(tr("Dump Base CXI file"));
                connect(dump_base_cxi, &QAction::triggered,
                        [this, specifier] { StartDumpingCXISingle(specifier); });
                QAction* build_base_cia = context_menu.addAction(tr("Build Base CIA"));
                connect(build_base_cia, &QAction::triggered,
                        [this, specifier] { StartBuildingCIASingle(specifier); });
            } else if (specifier.type == Core::ContentType::Update) {
                QAction* build_update_cia = context_menu.addAction(tr("Build Update CIA"));
                connect(build_update_cia, &QAction::triggered,
                        [this, specifier] { StartBuildingCIASingle(specifier); });
            } else if (specifier.type == Core::ContentType::DLC) {
                QAction* build_dlc_cia = context_menu.addAction(tr("Build DLC CIA"));
                connect(build_dlc_cia, &QAction::triggered,
                        [this, specifier] { StartBuildingCIASingle(specifier); });
            }
        }
    }
    context_menu.exec(ui->main->viewport()->mapToGlobal(point));
}

class AdvancedMenu : public QMenu {
public:
    explicit AdvancedMenu(QWidget* parent) : QMenu(parent) {}

private:
    void mousePressEvent(QMouseEvent* event) override {
        auto* dialog = static_cast<ImportDialog*>(parentWidget());
        // Block popup menu when clicking on the Advanced button to dismiss the menu.
        // With out this, it will immediately bring up the menu again.
        if (dialog->childAt(dialog->mapFromGlobal(event->globalPos())) ==
            dialog->ui->advanced_button) {

            dialog->block_advanced_menu = true;
        }

        QMenu::mousePressEvent(event);
    }
};

void ImportDialog::ShowAdvancedMenu() {
    if (block_advanced_menu) {
        block_advanced_menu = false;
        return;
    }

    AdvancedMenu menu(this);

    QAction* batch_dump_cxi = menu.addAction(tr("Batch Dump CXI"));
    connect(batch_dump_cxi, &QAction::triggered, this, &ImportDialog::StartBatchDumpingCXI);

    QAction* batch_build_cia = menu.addAction(tr("Batch Build CIA"));
    connect(batch_build_cia, &QAction::triggered, this, &ImportDialog::StartBatchBuildingCIA);

    menu.exec(ui->advanced_button->mapToGlobal(ui->advanced_button->rect().bottomLeft()));
}

static QString FormatETA(int eta) {
    if (eta < 0) {
        return QStringLiteral("&nbsp;");
    }
    return QCoreApplication::translate("ImportDialog", "ETA %1m%2s")
        .arg(eta / 60, 2, 10, QLatin1Char('0'))
        .arg(eta % 60, 2, 10, QLatin1Char('0'));
}

// Runs the job, opening a dialog to report is progress.
void ImportDialog::RunMultiJob(MultiJob* job, std::size_t total_count, u64 total_size) {
    // Try to map total_size to int range
    // This is equal to ceil(total_size / INT_MAX)
    const u64 multiplier =
        (total_size + std::numeric_limits<int>::max() - 1) / std::numeric_limits<int>::max();

    auto* label = new QLabel(tr("Initializing..."));
    label->setWordWrap(true);
    label->setFixedWidth(600);

    // We need to create the bar ourselves to circumvent an issue caused by modal ProgressDialog's
    // event handling.
    auto* bar = new QProgressBar(this);
    bar->setRange(0, static_cast<int>(total_size / multiplier));
    bar->setValue(0);

    auto* dialog = new QProgressDialog(tr("Initializing..."), tr("Cancel"), 0, 0, this);
    dialog->setBar(bar);
    dialog->setLabel(label);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);

    connect(job, &MultiJob::NextContent, this,
            [this, dialog, total_count](std::size_t count,
                                        const Core::ContentSpecifier& next_content, int eta) {
                dialog->setLabelText(
                    tr("<p>(%1/%2) %3 (%4)</p><p>&nbsp;</p><p align=\"right\">%5</p>")
                        .arg(count)
                        .arg(total_count)
                        .arg(GetContentName(next_content))
                        .arg(GetContentTypeName<false>(next_content.type))
                        .arg(FormatETA(eta)));
                current_content = next_content;
                current_count = count;
            });
    connect(job, &MultiJob::ProgressUpdated, this,
            [this, bar, dialog, multiplier, total_count](u64 current_imported_size,
                                                         u64 total_imported_size, int eta) {
                bar->setValue(static_cast<int>(total_imported_size / multiplier));
                dialog->setLabelText(tr("<p>(%1/%2) %3 (%4)</p><p align=\"center\">%5 "
                                        "/ %6</p><p align=\"right\">%7</p>")
                                         .arg(current_count)
                                         .arg(total_count)
                                         .arg(GetContentName(current_content))
                                         .arg(GetContentTypeName<false>(current_content.type))
                                         .arg(ReadableByteSize(current_imported_size))
                                         .arg(ReadableByteSize(current_content.maximum_size))
                                         .arg(FormatETA(eta)));
            });
    connect(job, &MultiJob::Completed, this, [this, dialog, job] {
        dialog->setValue(dialog->maximum());

        const auto failed_contents = job->GetFailedContents();
        if (failed_contents.empty()) {
            QMessageBox::information(this, tr("threeSD"), tr("All contents done successfully."));
        } else {
            QString list_content;
            for (const auto& content : failed_contents) {
                list_content.append(QStringLiteral("<li>%1 (%2)</li>")
                                        .arg(GetContentName(content))
                                        .arg(GetContentTypeName<false>(content.type)));
            }
            QMessageBox::critical(this, tr("threeSD"),
                                  tr("List of failed contents:<ul>%1</ul>").arg(list_content));
        }

        RelistContent();
    });
    connect(dialog, &QProgressDialog::canceled, this, [this, job] {
        // Add yet-another-ProgressDialog to indicate cancel progress
        auto* cancel_dialog = new QProgressDialog(tr("Canceling..."), tr("Cancel"), 0, 0, this);
        cancel_dialog->setWindowModality(Qt::WindowModal);
        cancel_dialog->setCancelButton(nullptr);
        cancel_dialog->setMinimumDuration(0);
        cancel_dialog->setValue(0);
        connect(job, &MultiJob::Completed, cancel_dialog, &QProgressDialog::hide);
        job->Cancel();
    });

    job->start();
}

void ImportDialog::StartImporting() {
    UpdateSizeDisplay();
    if (!ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)->isEnabled()) {
        // Space is no longer enough
        QMessageBox::warning(this, tr("Not Enough Space"),
                             tr("Your disk does not have enough space to hold imported data."));
        return;
    }

    auto to_import = GetSelectedContentList();
    const std::size_t total_count = to_import.size();

    auto* job =
        new MultiJob(this, *importer, std::move(to_import), &Core::SDMCImporter::ImportContent,
                     &Core::SDMCImporter::AbortImporting);

    RunMultiJob(job, total_count, total_selected_size);
}

// CXI dumping

void ImportDialog::StartDumpingCXISingle(const Core::ContentSpecifier& specifier) {
    const QString path = QFileDialog::getSaveFileName(this, tr("Dump CXI file"), last_dump_cxi_path,
                                                      tr("CTR Executable Image (*.cxi)"));
    if (path.isEmpty()) {
        return;
    }
    last_dump_cxi_path = QFileInfo(path).path();

    auto* job = new SimpleJob(
        this,
        [this, specifier, path](const Common::ProgressCallback& callback) {
            return importer->DumpCXI(specifier, path.toStdString(), callback);
        },
        [this] { importer->AbortDumpCXI(); });
    job->StartWithProgressDialog(this);
}

void ImportDialog::StartBatchDumpingCXI() {
    auto to_import = GetSelectedContentList();
    if (to_import.empty()) {
        QMessageBox::warning(this, tr("threeSD"),
                             tr("Please select the contents you would like to dump as CXIs."));
        return;
    }

    const auto removed_iter = std::remove_if(
        to_import.begin(), to_import.end(), [](const Core::ContentSpecifier& specifier) {
            return specifier.type != Core::ContentType::Application;
        });
    if (removed_iter == to_import.begin()) { // No Applications selected
        QMessageBox::critical(this, tr("threeSD"),
                              tr("The contents selected are not supported.<br>You can only dump "
                                 "Applications as CXIs."));
        return;
    }
    if (removed_iter != to_import.end()) { // Some non-Applications selected
        QMessageBox::warning(this, tr("threeSD"),
                             tr("Some contents selected are not supported and will be "
                                "ignored.<br>Only Applications will be dumped as CXIs."));
    }

    to_import.erase(removed_iter, to_import.end());

    QString path =
        QFileDialog::getExistingDirectory(this, tr("Batch Dump CXI"), last_batch_dump_cxi_path);
    if (path.isEmpty()) {
        return;
    }
    last_batch_dump_cxi_path = path;
    if (!path.endsWith(QChar::fromLatin1('/')) && !path.endsWith(QChar::fromLatin1('\\'))) {
        path.append(QStringLiteral("/"));
    }

    const auto total_count = to_import.size();
    const auto total_size = std::accumulate(to_import.begin(), to_import.end(), u64{0},
                                            [](u64 sum, const Core::ContentSpecifier& specifier) {
                                                return sum + specifier.maximum_size;
                                            });
    auto* job = new MultiJob(
        this, *importer, std::move(to_import),
        [path](Core::SDMCImporter& importer, const Core::ContentSpecifier& specifier,
               const Common::ProgressCallback& callback) {
            return importer.DumpCXI(specifier, path.toStdString(), callback, true);
        },
        &Core::SDMCImporter::AbortDumpCXI);
    RunMultiJob(job, total_count, total_size);
}

// CIA building

void ImportDialog::StartBuildingCIASingle(const Core::ContentSpecifier& specifier) {
    CIABuildDialog dialog(this,
                          /*is_dir*/ false,
                          /*is_nand*/ Core::IsNandTitle(specifier.type),
                          /*enable_legit*/ importer->CanBuildLegitCIA(specifier),
                          last_build_cia_path);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto& [path, type] = dialog.GetResults();
    last_build_cia_path = QFileInfo(path).path();

    auto* job = new SimpleJob(
        this,
        [this, specifier, path = path, type = type](const Common::ProgressCallback& callback) {
            return importer->BuildCIA(type, specifier, path.toStdString(), callback);
        },
        [this] { importer->AbortBuildCIA(); });
    job->StartWithProgressDialog(this);
}

void ImportDialog::StartBatchBuildingCIA() {
    auto to_import = GetSelectedContentList();
    if (to_import.empty()) {
        QMessageBox::warning(this, tr("threeSD"),
                             tr("Please select the contents you would like to build as CIAs."));
        return;
    }

    const auto removed_iter = std::remove_if(
        to_import.begin(), to_import.end(),
        [](const Core::ContentSpecifier& specifier) { return !Core::IsTitle(specifier.type); });
    if (removed_iter == to_import.begin()) { // No Titles selected
        QMessageBox::critical(this, tr("threeSD"),
                              tr("The contents selected are not supported.<br>You can only build "
                                 "CIAs from Applications, Updates, DLCs and System Titles."));
        return;
    }
    if (removed_iter != to_import.end()) { // Some non-Titles selected
        QMessageBox::warning(
            this, tr("threeSD"),
            tr("Some contents selected are not supported and will be ignored.<br>Only "
               "Applications, Updates, DLCs and System Titles will be built as CIAs."));
    }

    to_import.erase(removed_iter, to_import.end());

    const bool is_nand = std::all_of(
        to_import.begin(), to_import.end(),
        [](const Core::ContentSpecifier& specifier) { return Core::IsNandTitle(specifier.type); });
    const bool enable_legit = std::all_of(to_import.begin(), to_import.end(),
                                          [this](const Core::ContentSpecifier& specifier) {
                                              return importer->CanBuildLegitCIA(specifier);
                                          });
    CIABuildDialog dialog(this, /*is_dir*/ true, is_nand, enable_legit, last_batch_build_cia_path);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    auto [path, type] = dialog.GetResults();
    last_batch_build_cia_path = path;
    if (!path.endsWith(QChar::fromLatin1('/')) && !path.endsWith(QChar::fromLatin1('\\'))) {
        path.append(QStringLiteral("/"));
    }

    const auto total_count = to_import.size();
    const auto total_size = std::accumulate(to_import.begin(), to_import.end(), u64{0},
                                            [](u64 sum, const Core::ContentSpecifier& specifier) {
                                                return sum + specifier.maximum_size;
                                            });
    auto* job = new MultiJob(
        this, *importer, std::move(to_import),
        [path = path, type = type](Core::SDMCImporter& importer,
                                   const Core::ContentSpecifier& specifier,
                                   const Common::ProgressCallback& callback) {
            return importer.BuildCIA(type, specifier, path.toStdString(), callback, true);
        },
        &Core::SDMCImporter::AbortBuildCIA);
    RunMultiJob(job, total_count, total_size);
}
